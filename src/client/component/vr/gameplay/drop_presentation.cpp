#include <std_include.hpp>
#include "drop_presentation.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include <utils/native_memory.hpp>
#include "component/scene_models.hpp"
#include "component/fastfiles.hpp"
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::drop_presentation
{
	namespace
	{
		using clock = std::chrono::steady_clock;
		struct handoff
		{
			native_carry::world_key key{};
			native_carry::model_geometry model{};
			hands::anchor world{};
			clock::time_point at{};
			bool active{};
		};
		std::array<handoff,8> handoffs{};
		std::array<unsigned short,8*32> lighting{};
		std::mutex mutex;
		utils::hook::detour native_submit;
		std::atomic_uint64_t started{}, bridged{}, completed{}, expired{};
		std::atomic_bool has_pending{};
		void submit_native(const void* object, void* pose, unsigned entity, unsigned flags,
			float* light, float time, std::int64_t a, std::int64_t b)
		{
			native_submit.invoke<void>(object,pose,entity,flags,light,time,a,b);
			if (!has_pending.load(std::memory_order_relaxed)) return;
			// Scene submission is the handoff boundary, not server entity creation.
			// It runs before the scene sorter/our preview submission in this frame.
			const std::lock_guard lock(mutex);
			for (auto& v:handoffs) if (v.active && unsigned(v.key.entity)==entity &&
				native_carry::entity_key(v.key.entity)==v.key)
			{
				const game::XModel* const* models{};const game::XModel* first{};
				if (object && utils::native_memory::read_bytes(&models,static_cast<const std::byte*>(object)+0xd8,sizeof(models)) &&
					utils::native_memory::read_bytes(&first,models,sizeof(first)) && first==(v.model.native_root ? v.model.native_root : v.model.parts[0].model))
				{v.active=false;++completed;}
			}
		}
		void submit()
		{
			if (!has_pending.load(std::memory_order_relaxed) || !game::CL_IsCgameInitialized()) return;
			std::array<handoff,8> batch;
			{
				const std::lock_guard lock(mutex);
				const auto now=clock::now();
				for (auto& v:handoffs) if (v.active && (now<v.at || now-v.at>250ms))
				{v.active=false;++expired;}
				batch=handoffs;
				has_pending=std::any_of(handoffs.begin(),handoffs.end(),[](const auto& v){return v.active;});
			}
			for (size_t i=0;i<batch.size();++i)
			{
				const auto& v=batch[i];if (!v.active) continue;
				for (size_t n=0;n<v.model.count;++n)
				{
					const auto& part=v.model.parts[n];
					const auto world=hands::pose_math::compose(v.world,part.local);
					game::GfxScaledPlacement p{};p.scale=1;
					std::copy(world.position.begin(),world.position.end(),p.base.origin);
					std::copy(world.rotation.begin(),world.rotation.end(),p.base.quat);
					float color[4]{1,1,1,1};
					scene_models::submit(part.model,&p,1,&lighting[i*32+n],color,color,color,0);
				}
				++bridged;
			}
		}
	}
	void begin(native_carry::world_key key,const native_carry::model_geometry& model,const hands::anchor& world) noexcept
	{
		if (!model.valid || !model.count || model.count>32 || key.entity<=0) return;
		const std::lock_guard lock(mutex);
		for (auto& v:handoffs) if (!v.active) {v={key,model,world,clock::now(),true};has_pending=true;++started;return;}
	}
	void update(native_carry::world_key key,const hands::anchor& world) noexcept
	{const std::lock_guard lock(mutex);for(auto& v:handoffs) if(v.active && v.key==key)v.world=world;}
	bool pending(native_carry::world_key key) noexcept
	{if(!has_pending.load(std::memory_order_relaxed))return false;const std::lock_guard lock(mutex);for(const auto& v:handoffs) if(v.active && v.key==key)return true;return false;}
	void cancel(native_carry::world_key key) noexcept
	{const std::lock_guard lock(mutex);for(auto& v:handoffs) if(v.key==key)v.active=false;}
	void clear() noexcept {const std::lock_guard lock(mutex);handoffs={};has_pending=false;}
	std::string status() {return std::format("drop_handoff started={} preview_frames={} native_submitted={} expired={}\n",started.load(),bridged.load(),completed.load(),expired.load());}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			constexpr std::uint8_t bytes[]{0x40,0x55,0x53,0x56,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xf1};
			std::array<std::uint8_t,sizeof(bytes)> mask{};mask.fill(0xff);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140775C40),{bytes,mask.data(),mask.size()}))
				throw std::runtime_error("native item scene handoff contract rejected");
			native_submit.create(0x140775C40,submit_native);
			scene_models::on_submit(submit);
			fastfiles::on_pre_unload([] {clear();});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::drop_presentation::component)
