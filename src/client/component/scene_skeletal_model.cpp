#include <std_include.hpp>
#include "scene_skeletal_model.hpp"
#include "scheduler_context.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <mutex>

namespace scene_models
{
	namespace
	{
		constexpr std::uintptr_t create_object=0x140653180,free_object=0x1406539D0;
		constexpr std::uintptr_t calc_pose=0x14038C9E0,lock_object=0x140659010,unlock_object=0x1406596C0;
		// R_AddDObjToScene tests bit 13 before its single-model/no-XAnim-tree
		// shortcut. That shortcut renders only the cpose and discards our bones.
		constexpr unsigned force_skeletal_scene=0x2000;
		std::mutex registry_mutex;
		std::array<skeletal_model*,skeletal_model::capacity> registry{};
		std::array<std::atomic<const void*>,skeletal_model::capacity> objects{};
		std::atomic_bool skeleton_hook_ready{};
		template<size_t N>bool check(std::uintptr_t address,const std::array<std::uint8_t,N>& bytes)
		{std::array<std::uint8_t,N> mask;mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes.data(),mask.data(),N}));}
		bool native_ready()
		{
			static const bool ready=check(create_object,std::array<std::uint8_t,9>{0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55}) &&
				check(free_object,std::array<std::uint8_t,9>{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9}) &&
				check(calc_pose,std::array<std::uint8_t,10>{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10}) &&
				check(lock_object,std::array<std::uint8_t,10>{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20}) &&
				check(unlock_object,std::array<std::uint8_t,8>{0xc7,0x41,0x18,0,0,0,0,0xc3}) &&
				check(0x140775C6E,std::array<std::uint8_t,15>{0x41,0x8b,0xd9,0x45,0x8b,0xe8,0xc1,0xeb,0x0d,0x41,0x8b,0xf1,0x83,0xe3,0x01}) &&
				check(0x140775CFF,std::array<std::uint8_t,8>{0x85,0xdb,0x0f,0x85,0x4a,0x01,0x00,0x00}) &&
				check(0x1403D5A19,std::array<std::uint8_t,8>{0xc7,0x44,0x24,0x2c,0xc0,0x0f,0,0});
			return ready;
		}
	}
	struct skeletal_model::storage
	{
		alignas(16) std::array<std::byte,0x240> object{};
		struct pose_storage
		{
			std::byte header[0x1c]{};
			std::array<float,3> origin{},angles{},previous_origin{},previous_angles{};
			std::byte tail[0xb8-0x4c]{};
		} pose;
		static_assert(sizeof(pose_storage)==0xb8 && offsetof(pose_storage,origin)==0x1c);
		std::mutex mutex;
		std::array<game::DObjAnimMat,255> bones{};
		unsigned count{},slot{};
		const void* accepted{};
		bool visible{},created{};
	};
	static_assert(skeletal_model::scene_begin+skeletal_model::capacity<=4032);
	skeletal_model::skeletal_model()=default;
	void skeletal_model::enable() noexcept {skeleton_hook_ready=true;}
	skeletal_model::~skeletal_model()
	{
		if(!data_)return;
		// Owner retires only at the native asset/render drain, like carried DObjs.
		{const std::lock_guard lock(registry_mutex);objects[data_->slot]=nullptr;registry[data_->slot]=nullptr;}
		if(data_->created)utils::hook::invoke<void>(free_object,data_->object.data());
	}
	bool skeletal_model::create(game::XModel* model)
	{
		if(data_ || !skeleton_hook_ready || !scheduler::is_executing(scheduler::pipeline::main) || !native_ready() || !model || !model->numBones || !model->baseMat || model->numRootBones!=1)return false;
		const std::lock_guard lock(registry_mutex);
		const auto free=std::find(registry.begin(),registry.end(),nullptr);if(free==registry.end())return false;
		auto next=std::make_unique<storage>();next->count=model->numBones;next->slot=unsigned(free-registry.begin());
		struct model_entry{game::XModel* model;std::uint32_t tag{};std::uint8_t collision{};std::byte pad[3]{};};
		static_assert(sizeof(model_entry)==16);const model_entry entry{model};
		utils::hook::invoke<void>(create_object,&entry,1,nullptr,next->object.data(),0);next->created=true;
		if(std::to_integer<unsigned>(next->object[0x10])!=next->count){utils::hook::invoke<void>(free_object,next->object.data());return false;}
		data_=std::move(next);*free=this;objects[data_->slot]=data_->object.data();return true;
	}
	bool skeletal_model::owns(const void* object) noexcept
	{
		if(!object)return false;
		for(const auto& item:objects)if(item.load()==object)return true;return false;
	}
	void skeletal_model::apply(void* object) noexcept
	{
		const std::lock_guard lock(registry_mutex);
		for(auto* item:registry)if(item && item->data_->object.data()==object)
		{
			auto& data=*item->data_;const std::lock_guard pose_lock(data.mutex);
			data.accepted=nullptr;
			game::DObjAnimMat* matrices{};std::memcpy(&matrices,data.object.data()+0xa8,sizeof(matrices));
			const auto* view=*reinterpret_cast<const std::byte* const*>(0x141E39D30);
			if(!matrices || !view)return;
			std::array<float,3> offset{};std::memcpy(offset.data(),view+0x58,sizeof(offset));
			for(float x:offset)if(!std::isfinite(x) || std::abs(x)>1e7f)return;
			// DObj matrices are relative to the renderer's origin, which can be
			// far from the tracked head after crouching or room-scale movement.
			// Keep the stored snapshot in world space so repeated applies cannot
			// subtract an origin twice or retain a previous render-frame origin.
			for(unsigned i=0;i<data.count;++i)for(unsigned axis=0;axis<3;++axis)
				if(std::abs(data.bones[i].trans[axis]-offset[axis])>10000)return;
			std::copy_n(data.bones.begin(),data.count,matrices);
			for(unsigned i=0;i<data.count;++i)for(unsigned axis=0;axis<3;++axis)matrices[i].trans[axis]-=offset[axis];
			data.accepted=matrices;return;
		}
	}
	bool skeletal_model::submit(std::span<const game::DObjAnimMat> bones,unsigned flags)
	{
		if(!data_ || bones.size()!=data_->count)return false;
		for(const auto& b:bones)
		{
			float norm{};for(float x:b.quat){if(!std::isfinite(x))return false;norm+=x*x;}
			if(norm<.5f || norm>1.5f)return false;
			for(float x:b.trans)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
		}
		auto& data=*data_;
		{const std::lock_guard lock(data.mutex);std::copy(bones.begin(),bones.end(),data.bones.begin());data.accepted=nullptr;}
		const std::array<float,3> origin{bones[0].trans[0],bones[0].trans[1],bones[0].trans[2]};
		data.pose.previous_origin=data.visible?data.pose.origin:origin;data.pose.origin=origin;
		alignas(16) std::array<std::uint32_t,8> bits{};
		for(unsigned b=0;b<data.count;++b)bits[b/32]|=0x80000000u>>(b%32);
		utils::hook::invoke<void>(lock_object,data.object.data());
		const auto* matrices=utils::hook::invoke<const game::DObjAnimMat*>(calc_pose,&data.pose,data.object.data(),bits.data());
		// An already-calculated epoch may skip the skeleton hook on a second
		// submission. Apply this snapshot while still holding its native lock.
		if(matrices)apply(data.object.data());
		bool accepted{};{const std::lock_guard lock(data.mutex);accepted=matrices && matrices==data.accepted;}
		utils::hook::invoke<void>(unlock_object,data.object.data());
		if(!accepted){data.visible=false;return false;}
		auto light=origin;game::R_AddDObjToScene(data.object.data(),&data.pose,scene_begin+data.slot,flags|force_skeletal_scene,light.data(),0.f,0,0);
		data.visible=true;return true;
	}
}
