#include <std_include.hpp>
#include "scene_models.hpp"
#include "scene_model_culling.hpp"
#include "scene_model_identity.hpp"
#include "scene_model_identity_bridge.hpp"
#include "scheduler_context.hpp"
#include "fastfiles.hpp"
#include "game/xmodel_index_contract.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/startup_callbacks.hpp>

namespace scene_models
{
	namespace
	{
		utils::hook::detour generate_hook;
		utils::hook::detour prepare_hook;
		utils::hook::detour model_index_hook;
		// Cover all retained subset producers together; admission must not depend
		// on which weapon family happened to prepare its independent parts first.
		// Counted magazines retain four states per receiver alongside chamber,
		// action and equipment models. Keep this shared budget bounded and large
		// enough for the comprehensive weapon scene without evicting queued models.
		identity::registry<512> model_identities;
		std::atomic_size_t registration_failures{},asset_retirements{};
		identity::pool model_pool() noexcept
		{
			using namespace game::xmodel_index_contract;
			const auto base=reinterpret_cast<std::uintptr_t>(game::g_assetPool[game::ASSET_TYPE_XMODEL]);
			const auto count=game::g_poolSize[game::ASSET_TYPE_XMODEL];
			if (!base || base>UINTPTR_MAX-pool_header || count<=0) return {};
			return {base+pool_header,static_cast<std::size_t>(count),pool_stride};
		}
		std::uintptr_t canonical_model(std::uintptr_t model,std::uintptr_t native_first) noexcept
		{
			const auto pool=model_pool();
			if (pool.first==native_first)
				if (const auto source=model_identities.source(pool,model)) return *source;
			// Never clamp arbitrary engine pointers to slot zero. Unregistered stock
			// calls preserve native behavior; our submit boundary rejects such inputs.
			return model;
		}
		utils::startup_callbacks<placement_callback> placement_consumers;
		std::atomic<build_observer> build_observations{};
		std::atomic<scene_observer> scene_observations{};
		std::atomic_uint64_t scene_observation_sequence{};
		thread_local preparation preparing{};
		struct culling_scope { game::XModel* model{}; float padding{}; };
		thread_local culling_scope culling{};
		float filter_radius(game::XModel* model)
		{
			const auto radius = reinterpret_cast<float(*)(game::XModel*)>(0x14066E0C0)(model);
			const auto padded=radius + (model == culling.model ? culling.padding : 0.f);
			return std::isfinite(padded) ? padded : radius;
		}
		void prepare(void* command)
		{
			const auto before = preparing;
			const auto leave = gsl::finally([&] { preparing = before; });
			preparing = {};
			if (command)
			{
				std::memcpy(&preparing.record,command,sizeof(void*));
			}
			prepare_hook.invoke<void>(command);
		}
		using build_fn = int(*)(void*, game::XModel*, void*, const game::GfxPlacement*, float,
			const game::GfxPlacement*, float, bool, int, unsigned char, void*);
		int build(void* output, game::XModel* model, void* object, const game::GfxPlacement* current,
			float scale, const game::GfxPlacement* previous, float previous_scale, bool motion,
			int lod, unsigned char material_lod, void* colors)
		{
			game::GfxPlacement placed{}, history{};
			const auto observe=build_observations.load(std::memory_order_acquire);
			build_observation observation{preparing.record,output,model,nullptr,current,current,build_outcome::retained,0};
			const auto finish=[&](int result) {
				if (observe && observation.owner)
				{
					observation.resolved=current;observation.native_result=result;observe(observation);
				}
				return result;
			};
			if (!placement_consumers.empty() && preparing.record && current && previous)
			{
				// Native skeletal preparation may have taken time since job entry.
				// Sample once at the first rigid consumer, then reuse for every part.
				if(preparing.at==std::chrono::steady_clock::time_point{})preparing.at=std::chrono::steady_clock::now();
				placed = *current; history = *previous;
				bool replaced=false;
				for (const auto consumer:placement_consumers)
				{
					const auto result=consumer(preparing,output,placed,history);
					if (result!=placement_result::unchanged) observation.owner=consumer;
					if (result==placement_result::omit)
					{observation.outcome=build_outcome::omitted;return finish(0);}
					if (result==placement_result::replace)
					{
						if (replaced)
						{observation.outcome=build_outcome::conflicting;return finish(0);}
						replaced=true; current=&placed; previous=&history;
						observation.outcome=build_outcome::replaced;
					}
				}
			}
			// Native copies quaternion and fixed-point position into the surface
			// payload, including optional motion history; it retains neither pointer.
			return finish(reinterpret_cast<build_fn>(0x14075C580)(output,model,object,current,scale,
				previous,previous_scale,motion,lod,material_lod,colors));
		}
		utils::startup_callbacks<callback> callbacks;
		std::atomic_bool installed{};
		void generate(std::uint32_t client, std::uint32_t index, void* scratch, void* selected, void* slot, void* output)
		{
			const auto observe=scene_observations.load(std::memory_order_acquire);
			scene_observation observation{0,0,index,scratch,slot};
			if (observe) {observation.sequence=++scene_observation_sequence;observe(observation);}
			for (const auto callback:callbacks) callback();
			if (observe) {observation.stage=1;observe(observation);}
			generate_hook.invoke<void>(client, index, scratch, selected, slot, output);
			if (observe) {observation.stage=2;observe(observation);}
		}
	}
	void on_submit(callback function)
	{
		callbacks.add(function);
	}
	void observe_builds(build_observer observer) noexcept
	{
		build_observations.store(observer,std::memory_order_release);
	}
	void observe_scenes(scene_observer observer) noexcept
	{
		scene_observations.store(observer,std::memory_order_release);
	}
	bool ready() noexcept { return installed.load(); }
	bool register_runtime_models(std::span<const runtime_model> models) noexcept
	{
		// A precomputed immutable animation is registered atomically, so a
		// rejected clip cannot leave aliases pointing to half-retired frames.
		if (!ready() || !scheduler::is_executing(scheduler::pipeline::main) || models.empty() || models.size()>128) return false;
		std::array<identity::alias,128> batch{};
		for (std::size_t n=0;n<models.size();++n)
			batch[n]={reinterpret_cast<std::uintptr_t>(models[n].descriptor),reinterpret_cast<std::uintptr_t>(models[n].source)};
		const bool accepted=model_identities.publish(model_pool(),{batch.data(),models.size()});
		if (!accepted) ++registration_failures;
		return accepted;
	}
	std::string runtime_model_status()
	{
		return std::format("runtime_models={}/256 registration_failures={} asset_retirements={}\n",
			model_identities.size(),registration_failures.load(),asset_retirements.load());
	}
	void submit(game::XModel* model, game::GfxScaledPlacement* placement, unsigned flags,
		unsigned short* lighting_handle, float* lit, float* unlit, float* emissive, float padding)
	{
		if (!ready() || !model || !placement ||
			!model_identities.source(model_pool(),reinterpret_cast<std::uintptr_t>(model))) return;
		const auto before = culling;
		const auto leave = gsl::finally([&] { culling = before; });
		// Native scales the returned radius. Convert the bounded WORLD-unit
		// allowance to local units; an invalid scale keeps native behavior.
		culling = {model, local_radius_padding(padding,placement->scale)};
		game::R_FilterXModelIntoScene(model,placement,flags,lighting_handle,lit,unlit,emissive);
	}
	void on_prepare_placement(placement_callback function)
	{
		placement_consumers.add(function);
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			fastfiles::on_pre_unload([] {
				// Same drained native boundary used by DObj and surface-subset
				// owners. Pool addresses alone do not describe an asset lifetime.
				model_identities.reset_after_drain();++asset_retirements;
			});
			using namespace game::xmodel_index_contract;
			static_assert(sizeof(game::XModel)==pool_stride);
			// Hook AFTER the pool-base LEA: fastfiles owns its relocatable operand.
			// This ordering works whether that component has relocated the pool yet
			// or not. All native index callers retain the original leaf result/ABI.
			constexpr std::uint8_t lea[]{0x48,0x8d,0x05}, lea_mask[]{0xff,0xff,0xff};
			std::array<std::uint8_t,identity::getter_tail.size()> tail_mask{}; tail_mask.fill(0xff);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(getter),{lea,lea_mask,3}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(mapping_boundary),
					{identity::getter_tail.data(),tail_mask.data(),tail_mask.size()}))
				throw std::runtime_error("native XModel resource identity contract rejected");
			auto* bridge=utils::hook::assemble([](utils::hook::assembler& a) {
				identity::emit_bridge(a,reinterpret_cast<std::uintptr_t>(canonical_model));
			});
			if (!bridge) throw std::runtime_error("native XModel resource identity bridge allocation failed");
			model_index_hook.create(mapping_boundary,bridge);
			// Existing GUI model-preview boundary, centralized so gameplay and
			// tooling do not stack detours or depend on each other's state.
			constexpr std::uint8_t bytes[]{0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xf8};
			std::array<std::uint8_t, sizeof(bytes)> mask{}; mask.fill(0xff);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140778E65), {bytes,mask.data(),mask.size()}))
				throw std::runtime_error("native scene model submission contract rejected");
			constexpr std::uint8_t prepare_bytes[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
			std::array<std::uint8_t,sizeof(prepare_bytes)> prepare_mask{}; prepare_mask.fill(0xff);
			constexpr std::uint8_t build_call[]{0xe8,0x77,0x89,0x03,0x00};
			constexpr std::uint8_t call_mask[]{0xff,0xff,0xff,0xff,0xff};
			// Live R_FilterXModelIntoScene scales this radius and adds it to the
			// plane distance in its frustum admission loop. Do not patch the shared
			// getter or mutate XModel data retained by other queued submissions.
			constexpr std::uint8_t radius_call[]{0xe8,0x65,0x96,0xf4,0xff};
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140723380),
				{prepare_bytes,prepare_mask.data(),prepare_mask.size()}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140723C04),
				{build_call,call_mask,5}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140724A56),{radius_call,call_mask,5}) ||
				!utils::hook_validation::validate_executable_target(reinterpret_cast<void*>(0x14066E0C0)) ||
				!utils::hook_validation::validate_executable_target(reinterpret_cast<void*>(0x14075C580)))
				throw std::runtime_error("native rigid-surface preparation contract rejected");
			generate_hook.create(0x140778E60, generate);
			prepare_hook.create(0x140723380,prepare);
			utils::hook::call(0x140723C04,build);
			utils::hook::call(0x140724A56,filter_radius);
			installed = true;
		}
		void pre_destroy() override { installed = false; }
	};
}
REGISTER_COMPONENT(scene_models::component)
