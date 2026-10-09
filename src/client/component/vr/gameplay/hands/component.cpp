#include <std_include.hpp>
#include "status.hpp"
#include "../../h2/entrypoints.hpp"
#include "component/vr/gameplay/hands/rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "../vehicles/runtime.hpp"
#include "../native_shield.hpp"
#include "../native_scripted_control.hpp"
#include "../mounted_turret.hpp"
#include "../weapon_carry_runtime.hpp"
#include "../weapon_carry_pose.hpp"
#include "../../controller_input.hpp"
#include "../animation_presentation.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/fastfiles.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "../grip_presenter.hpp"
#include "native_rig.hpp"
#include "../scripted_arms_runtime.hpp"
#include "../campaign/cliffhanger/physical.hpp"
#include "../ladder_runtime.hpp"
#include "../campaign/ending/runtime.hpp"
#include "../forearm_twist.hpp"
#include "position_offset.hpp"
#include "../trigger_discipline.hpp"
#include "component/scene_skeletal_model.hpp"
#include "../empty_hands_native.hpp"
#include "../native_viewmodel_policy.hpp"
#include "loader/component_loader.hpp"
#include "../shoulder_anchors.hpp"
#include "../weapon_interaction.hpp"
#include "../weapon_recoil.hpp"
#include "../weapon_render_pose.hpp"
#include "../optic_runtime.hpp"
#include "../weapon_profiles.hpp"
#include "../physical_reload_presenter.hpp"
#include "../stowed_reload_pose.hpp"
#include "../magazine_grip_selection.hpp"
#include "../cylinder_presenter.hpp"
#include "../tube_presenter.hpp"
#include "../break_action_presenter.hpp"
#include "../launcher_presenter.hpp"
#include "../heartbeat_runtime.hpp"
#include "../underbarrel_runtime.hpp"
#include "../hand_interaction/runtime.hpp"
#include "../equipment_runtime.hpp"
#include "attachment_pose.hpp"
#include "../viewmodel_visibility.hpp"
#include "../secondary_motion.hpp"
#include <mutex>
#include <sstream>
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::hands
{
	namespace
	{
		constexpr std::uintptr_t calc_skeleton_address = 0x140657D80;
		constexpr std::uintptr_t client_object_address = vr::h2::sp::client_entity_dobj.address();
		constexpr int primary_viewmodel_handle = 4000;
		using object_layout = native_object;
		utils::hook::detour calc_skeleton_hook;
		game::dvar_t* enabled{};
		game::dvar_t* shoulder_half_width{};
		game::dvar_t* shoulder_down{};
		game::dvar_t* shoulder_back{};
		game::dvar_t* grips_enabled{};
		bool animation_query_ready{};
		std::atomic<bool> alive{true};
		std::atomic_uint64_t asset_generation{1};
		std::mutex mutex;
		thread_local bool in_hook{};
		std::uint64_t calls{}, applications{}, skipped{}, limited_left{}, limited_right{}, transition_poses{};
		const char* reason = "waiting for first-person skeleton";
		bool installed{};
		struct rig_binding
		{
			rig layout{};
			const weapons::profile* active_profile{};
			pose_library pose_library_data;
			forearm_twist_binding twist_binding;
			std::array<quat, 2> empty_wrist_basis{};
			std::array<bone, 256> empty_bind{};
			vec empty_head_anchor{};
			std::uint64_t assembly_key{};
			weapons::physical_reload::part_rig mechanical_parts{};
			weapons::ejection_port ejection{};
			weapons::cylinder::part_rig cylinder_parts{};
			weapons::break_action::part_rig break_parts{};
			weapons::launcher::part_rig launcher_parts{};
			weapons::tube::part_rig tube_parts{};
			weapons::heartbeat::part_rig heartbeat_parts{};
			weapons::underbarrel::part_rig underbarrel_parts{};
			hands::interaction_rig equipment_parts{};
			weapons::part_mask assembly_hidden{};
			weapons::optics::binding optic{};
			weapons::ads_comfort::binding ads_sight{};
			weapons::secondary_motion::binding secondary_parts{};
			std::string_view contract;
			float muzzle_forward_dot{};
			std::array<char, 256> hands_name{}, weapon_name{};
			const char* reason{"native model metadata rejected"};
			const char* grip_reason{"not evaluated"};
		};
		struct solver_state
		{
			rig_binding binding;
			rig_binding_cache binding_cache;
			weapons::grip_presenter grip_presenter;
			trigger_discipline::controller trigger_finger;
			float safe_index_weight{};
			bool safe_index_applied{};
			forearm_twist twist;
			weapons::ads_comfort::transition ads_transition;
			std::array<part_hand_transition, 2> part_motion;
			float ads_approach_meters{};
			weapons::secondary_motion::chain secondary_chain;
			object_layout* last_object{};
			bone* last_matrices{};
			std::uint32_t last_timestamp{};
			std::uint64_t last_resource_generation{};
			std::uint64_t last_asset_generation{};
			std::array<std::uint32_t, 8> last_bits{};
			std::array<game::XModel*, 32> last_models{};
			std::uint32_t last_duplicate_parts{};
			std::uint8_t last_model_count{}, last_bone_count{};
		};
		solver_state native_solver, empty_solver;
		std::array<solver_state, weapons::carry::visible_instance_capacity> owned_solvers;
		thread_local solver_state* selected_solver = &native_solver;
		solver_state& solver() noexcept
		{
			return *selected_solver;
		}
		struct pose_probe
		{
			std::uint64_t input_sequence{};
			double input_age_ms{}, camera_age_ms{};
			std::array<vec, 2> target{}, native_wrist{}, solved_wrist{};
			std::array<vec, 2> shoulder_target{}, native_shoulder{}, solved_shoulder{};
			std::array<vec, 2> native_elbow{}, solved_elbow{};
			vec head_position{};
			shoulder_offsets offsets{};
			bool solved{};
			int model_count{}, bone_count{};
			std::string_view contract;
			float muzzle_forward_dot{};
			weapons::hold owner{};
			const char* grip_reason{"not evaluated"};
			weapons::grip_result grip{};
			bool equip_suppressed{};
			unsigned forearm_twist_hands{};
			body_pose::estimate storage_body{};
			std::array<char, 256> hands_name{}, weapon_name{};
			position_offsets wrist_offsets{};
		};
		pose_probe probe{};

		bool rebuild_rig(const object_layout& object, rig& result, bool empty)
		{
			probe.model_count = object.model_count;
			probe.bone_count = object.bone_count;
			reason = "native model metadata rejected";
			if (!object.models || object.model_count < (empty ? 1 : 2) || object.model_count > 32 ||
			    !object.bone_count)
				return false;
			std::array<model_definition, 32> models{};
			std::array<bone_definition, 256> bones{};
			if (!describe(object, models, bones))
				return false;
			const bool shield_model =
			    std::any_of(models.begin(),
			                models.begin() + object.model_count,
			                [](const auto& m) { return m.name == "h2_viewmodel_riot_shield_mp"; });
			const bool melee_model =
			    std::any_of(models.begin(),
			                models.begin() + object.model_count,
			                [](const auto& m) { return !weapons::special_melee::root(m.name).empty(); });
			if (shield_model && !weapons::shield::enabled())
			{
				reason = "shield native adapter unavailable";
				return false;
			}
			const auto resolved = resolve_rig({models.data(), object.model_count},
			                                  {bones.data(), object.bone_count},
			                                  empty          ? rig_kind::hands_only
			                                  : melee_model  ? rig_kind::melee
			                                  : shield_model ? rig_kind::shield
			                                                 : rig_kind::firearm);
			probe.muzzle_forward_dot = resolved.muzzle_forward_dot;
			if (resolved.hands_model >= 0)
			{
				const auto name = models[resolved.hands_model].name;
				std::copy(name.begin(), name.end(), probe.hands_name.begin());
			}
			if (resolved.weapon_model >= 0)
			{
				const auto name = models[resolved.weapon_model].name;
				std::copy(name.begin(), name.end(), probe.weapon_name.begin());
			}
			if (resolved.rejection)
			{
				reason = resolved.rejection;
				return false;
			}
			result = resolved.layout;
			const auto twists =
			    bind_forearm_twist(result, {bones.data(), object.bone_count}, models[resolved.hands_model]);
			solver().binding.twist_binding = twists;
			probe.contract = resolved.contract;
			{
				int head = -1;
				for (int i = 0; i < result.count; ++i)
				{
					solver().binding.empty_bind[i] = bones[i].bind;
					solver().binding.empty_bind[i].weight = 2.f;
					if (bones[i].name == "tag_view")
					{
						if (head >= 0)
						{
							reason = "ambiguous empty-hand view anchor";
							return false;
						}
						head = i;
					}
				}
				if (head < 0)
				{
					reason = "empty-hand view anchor missing";
					return false;
				}
				solver().binding.empty_head_anchor = bones[head].bind.position;
				if (!empty)
				{
					for (int m = 0; m < object.model_count; ++m)
					{
						const int root = models[m].begin, parent = bones[root].parent;
						if (parent < 0)
							continue;
						using namespace hands::pose_math;
						const auto transform = compose(as_anchor(solver().binding.empty_bind[parent]),
						                               inverse(as_anchor(bones[root].bind)));
						for (int i = root; i < root + models[m].count; ++i)
						{
							const auto bind = compose(transform, as_anchor(bones[i].bind));
							solver().binding.empty_bind[i].position = bind.position;
							solver().binding.empty_bind[i].rotation = bind.rotation;
						}
					}
				}
			}
			solver().binding.equipment_parts =
			    hands::bind_interaction_rig(result, {bones.data(), object.bone_count});
			if (empty)
			{
				// The native hand bind pose faces its own weapon reference. Reuse
				// that anatomical wrist basis and its relaxed fingers, for this skin.
				const auto reference = conjugate(normalize(bones[result.weapon_tag].bind.rotation));
				for (int h = 0; h < 2; ++h)
					solver().binding.empty_wrist_basis[h] =
					    normalize(multiply(reference, bones[result.arms[h].wrist].bind.rotation));
				probe.grip_reason = "native relaxed hand bind pose";
				return true;
			}
			const auto match = weapons::select_profile(
			    {models.data(), object.model_count}, result, {bones.data(), object.bone_count});
			if ((shield_model || melee_model) && !match.value)
			{
				reason = match.reason;
				return false;
			}
			solver().binding.active_profile = match.value;
			solver().binding.ejection =
			    match.value && !match.value->melee && !match.value->defense && !match.value->cylinder &&
			            !match.value->break_open
			        ? weapons::bind_ejection_port(result, {bones.data(), object.bone_count})
			        : weapons::ejection_port{};
			probe.grip_reason = match.reason;
			solver().binding.assembly_key = 14695981039346656037ull;
			for (size_t m = 0; m < object.model_count; ++m)
			{
				for (const unsigned char c : models[m].name)
					solver().binding.assembly_key = (solver().binding.assembly_key ^ c) * 1099511628211ull;
				solver().binding.assembly_key = (solver().binding.assembly_key ^ 0xff) * 1099511628211ull;
			}
			if (solver().binding.active_profile)
			{
				solver().binding.optic = weapons::optics::bind({object.models, object.model_count},
				                                               {models.data(), object.model_count});
				solver().binding.ads_sight = weapons::optics::bind_comfort(
				    {object.models, object.model_count}, {models.data(), object.model_count});
				solver().binding.pose_library_data = bind_weapon_poses(
				    result, {bones.data(), object.bone_count}, *solver().binding.active_profile);
				solver().binding.heartbeat_parts =
				    weapons::heartbeat::bind_native({object.models, object.model_count},
				                                    {models.data(), object.model_count},
				                                    result,
				                                    {bones.data(), object.bone_count});
				solver().binding.underbarrel_parts = weapons::underbarrel::bind(
				    {models.data(), object.model_count}, result, {bones.data(), object.bone_count});
				if (solver().binding.active_profile->reload)
					solver().binding.mechanical_parts = weapons::physical_reload::bind_parts(
					    result, {bones.data(), object.bone_count}, *solver().binding.active_profile->reload);
				if (solver().binding.active_profile->cylinder)
					solver().binding.cylinder_parts =
					    weapons::cylinder::bind_parts(result, {bones.data(), object.bone_count});
				if (solver().binding.active_profile->break_open)
					solver().binding.break_parts =
					    weapons::break_action::bind_parts(result,
					                                      {bones.data(), object.bone_count},
					                                      *solver().binding.active_profile->break_open);
				if (solver().binding.active_profile->launcher)
					solver().binding.launcher_parts =
					    weapons::launcher::bind_parts(result,
					                                  {bones.data(), object.bone_count},
					                                  *solver().binding.active_profile->launcher);
				if (solver().binding.active_profile->tube)
					solver().binding.tube_parts = weapons::tube::bind_parts(
					    result, {bones.data(), object.bone_count}, solver().binding.active_profile->tube);
				solver().binding.assembly_hidden = match.hidden;
				if (solver().binding.active_profile->secondary_motion)
					solver().binding.secondary_parts =
					    weapons::secondary_motion::bind(*solver().binding.active_profile->secondary_motion,
					                                    result,
					                                    {bones.data(), object.bone_count});
				if (!solver().binding.active_profile->viewmodel.hidden_attachments.empty() &&
				    !weapons::viewmodel_visibility::ready(
				        solver().binding.active_profile->viewmodel.visibility))
				{
					solver().binding.active_profile = nullptr;
					probe.grip_reason = "cosmetic visibility contract unavailable";
				}
			}
			if (solver().binding.active_profile && match.muzzle >= 0)
				result.muzzle = match.muzzle;
			return true;
		}
		bool build_rig(const object_layout& object,
		               rig& result,
		               std::uint64_t resource,
		               std::uint64_t assets,
		               bool empty = false)
		{
			using weapons::part_visibility;
			const unsigned admission =
			    (weapons::shield::enabled() ? 1u : 0u) |
			    (weapons::viewmodel_visibility::ready(part_visibility::surface) ? 2u : 0u) |
			    (weapons::viewmodel_visibility::ready(part_visibility::rigid_groups) ? 4u : 0u) |
			    (weapons::viewmodel_visibility::ready(part_visibility::skinned_groups) ? 8u : 0u);
			rig_binding_identity identity;
			auto& context = solver();
			if (!binding_identity(object, resource, assets, admission, empty, identity))
			{
				context.binding_cache.invalidate();
				context.binding = {};
				reason = "native model metadata rejected";
				return false;
			}
			if (!context.binding_cache.matches(identity))
			{
				const auto previous_twists = context.binding.twist_binding;
				context.binding = {};
				const bool accepted = rebuild_rig(object, context.binding.layout, empty);
				if (previous_twists != context.binding.twist_binding)
					context.twist.reset();
				auto& binding = context.binding;
				binding.contract = probe.contract;
				binding.muzzle_forward_dot = probe.muzzle_forward_dot;
				binding.hands_name = probe.hands_name;
				binding.weapon_name = probe.weapon_name;
				binding.reason = reason;
				binding.grip_reason = probe.grip_reason;
				context.binding_cache.store(identity, accepted);
			}
			const auto& binding = context.binding;
			probe.model_count = object.model_count;
			probe.bone_count = object.bone_count;
			probe.contract = binding.contract;
			probe.muzzle_forward_dot = binding.muzzle_forward_dot;
			probe.hands_name = binding.hands_name;
			probe.weapon_name = binding.weapon_name;
			probe.grip_reason = binding.grip_reason;
			reason = binding.reason;
			if (!context.binding_cache.accepted())
				return false;
			result = binding.layout;
			return true;
		}

		bool matches_weapon_model(const object_layout& object, const rig& layout, std::uint32_t weapon)
		{
			static_assert(offsetof(game::WeaponDef, gunModel) == 0x28);
			if (!weapon || weapon >= 512)
				return false;
			const auto* definition = game::weapon_defs[weapon];
			if (!definition || !definition->gunModel)
				return false;
			const game::XModel* receiver{};
			int begin{};
			for (int i = 0; i < object.model_count; ++i)
			{
				const auto* model = object.models[i];
				if (!model)
					return false;
				if (layout.gun >= begin && layout.gun < begin + model->numBones)
				{
					receiver = model;
					break;
				}
				begin += model->numBones;
			}
			if (!receiver)
				return false;
			std::array<const game::XModel*, 64> pending{}, seen{};
			unsigned count{}, visited{};
			for (unsigned i = 0; i < 2; ++i)
				if (definition->gunModel[i])
					pending[count++] = definition->gunModel[i];
			while (count)
			{
				const auto* model = pending[--count];
				if (model == receiver)
					return true;
				if (!model ||
				    std::find(seen.begin(), seen.begin() + visited, model) != seen.begin() + visited)
					continue;
				if (visited == seen.size())
					return false;
				seen[visited++] = model;
				if (model->numCompositeModels > 32 || count + model->numCompositeModels > pending.size())
					return false;
				if (model->numCompositeModels && !model->compositeModels)
					return false;
				for (unsigned i = 0; i < model->numCompositeModels; ++i)
					pending[count++] = model->compositeModels[i];
			}
			return false;
		}
		bool pose_stored(object_layout* object,
		                 const weapons::carry::storage_scene& storage,
		                 std::uint64_t resource,
		                 std::uint64_t assets)
		{
			using namespace weapons;
			using namespace hands::pose_math;
			if (!enabled || !enabled->current.enabled || !storage.item.id)
				return false;
			probe = {};
			probe.owner = storage.item.owner;
			const auto input = controller_input::latest();
			const auto now = controller_input::clock::now();
			head_pose_bridge::spatial_frame body;
			if (!input.focused || !input.sequence || now < input.sampled_at ||
			    now - input.sampled_at > 150ms || now < storage.at || now - storage.at > 150ms ||
			    !head_pose_bridge::get_spatial_frame(body) || body.generation != storage.reference ||
			    body.generation != input.reference_generation || now < body.captured_at ||
			    now - body.captured_at > 150ms)
				return false;
			rig layout;
			if (!build_rig(*object, layout, resource, assets))
				return false;
			const auto* profile = solver().binding.active_profile;
			if (!profile || !solver().binding.pose_library_data.valid ||
			    !viewmodel_visibility::ready(profile->viewmodel.visibility))
				return false;
			const auto* render_view = *reinterpret_cast<const std::byte* const*>(0x141E39D30);
			if (!render_view)
				return false;
			vec offset{};
			std::memcpy(offset.data(), render_view + 0x58, sizeof(offset));
			if (!std::all_of(offset.begin(),
			                 offset.end(),
			                 [](float x) { return std::isfinite(x) && std::abs(x) < 1e7f; }))
				return false;
			anchor gun;
			if (!carry::place_stowed_weapon(storage.item, *profile, body, storage.layout, gun))
				return false;
			gun.position = sub(gun.position, offset);
			alignas(16) std::array<std::uint32_t, 8> all{};
			for (int i = 0; i < layout.count; ++i)
				all[i / 32] |= 0x80000000u >> (i % 32);
			calc_skeleton_hook.invoke<void>(object, all.data());
			if (!object->matrices)
				return false;
			for (size_t i = 0; i < all.size(); ++i)
				if ((object->calculated[i] & all[i]) != all[i])
					return false;
			auto solved = solver().binding.empty_bind;
			if (!apply_weapon_rest(layout, solver().binding.pose_library_data, *profile, solved))
				return false;
			move_part(layout, layout.gun, gun, solved);
			auto hidden = solver().binding.assembly_hidden;
			const auto id = storage.item.id;
			if (profile->reload)
			{
				const auto view = physical_reload::current(id);
				const auto ammo =
				    physical_reload::stowed_ammunition(id, *profile->reload, view, storage.native_loaded);
				if (!ammo)
					return false;
				const auto result = physical_reload::pose_stowed(layout,
				                                                 solver().binding.mechanical_parts,
				                                                 *profile->reload,
				                                                 *ammo,
				                                                 body.units_per_meter,
				                                                 solved);
				if (!result.valid)
					return false;
				hidden = combine_part_masks(hidden, result.hidden);
			}
			if (profile->cylinder)
			{
				auto view = cylinder::current(id);
				if (!solver().binding.cylinder_parts.valid ||
				    (view.active && (view.owner.id() != id || view.definition != profile->cylinder)))
					return false;
				if (!view.active)
				{
					view.ammo = cylinder::import_native(
					    profile->cylinder->ammunition, id.weapon, id.generation, {storage.native_loaded, 0});
					view.active = true;
				}
				if (!cylinder::valid(profile->cylinder->ammunition, view.ammo))
					return false;
				const float opening =
				    view.ammo.phase == cylinder::action::open || view.ammo.phase == cylinder::action::opening
				        ? 1.f
				        : 0.f;
				cylinder::pose_parts(
				    layout, solver().binding.cylinder_parts, *profile->cylinder, opening, solved);
				hidden =
				    combine_part_masks(hidden, cylinder::hidden_parts(solver().binding.cylinder_parts, view));
			}
			if (profile->break_open)
			{
				auto view = break_action::current(id);
				if (!solver().binding.break_parts.valid ||
				    (view.active && (view.owner.id() != id || view.definition != profile->break_open)))
					return false;
				if (!view.active)
				{
					view.ammo = break_action::import_native(profile->break_open->ammunition,
					                                        id.weapon,
					                                        id.generation,
					                                        {storage.native_loaded, 0});
					view.active = true;
				}
				if (!break_action::valid(profile->break_open->ammunition, view.ammo))
					return false;
				hidden = combine_part_masks(hidden,
				                            break_action::pose_parts(layout,
				                                                     solver().binding.break_parts,
				                                                     *profile->break_open,
				                                                     view,
				                                                     view.ammo.hinge,
				                                                     solved));
			}
			const auto live = carry::stored_scene(id);
			if (live.item.at != storage.item.at || live.item.owner.revision != storage.item.owner.revision)
				return false;
			// The native whole-surface mask removes the entire hand model. Mechanical
			// masks retain their existing per-rigid/per-skinned group policy.
			part_mask native_hidden;
			auto* native_bits = reinterpret_cast<std::byte*>(object) + 0xb8;
			std::memcpy(native_hidden.data(), native_bits, sizeof(native_hidden));
			for (int i = 0; i < layout.count; ++i)
				if (!layout.weapon_bones[i])
					native_hidden[i / 32] |= 0x80000000u >> (i % 32);
			std::memcpy(native_bits, native_hidden.data(), sizeof(native_hidden));
			std::copy_n(solved.begin(), layout.count, object->matrices);
			viewmodel_visibility::publish(
			    object, object->matrices, object->timestamp, hidden, profile->viewmodel.visibility);
			owned_native::accept(object, object->matrices, object->timestamp);
			probe.solved = true;
			probe.input_sequence = input.sequence;
			return true;
		}
		void calc_skeleton_stub(object_layout* object, std::uint32_t* requested)
		{
			const auto original = [&] { calc_skeleton_hook.invoke<void>(object, requested); };
			if (object && requested && scene_models::skeletal_model::owns(object))
			{
				original();
				scene_models::skeletal_model::apply(object);
				return;
			}
			if (!in_hook && alive.load(std::memory_order_relaxed) && object && requested &&
			    ending::owns_pose(object) && ending::wants_pose(object, requested))
			{
				in_hook = true;
				const auto leave = gsl::finally([] { in_hook = false; });
				alignas(16) std::array<std::uint32_t, 8> all{};
				std::copy_n(requested, 8, all.begin());
				bool rebuilt = false;
				for (unsigned b = 0; b < object->bone_count; ++b)
					all[b / 32] |= 0x80000000u >> (b % 32);
				for (unsigned w = 0; w < all.size(); ++w)
					if ((object->calculated[w] & all[w]) != all[w])
						rebuilt = true;
				calc_skeleton_hook.invoke<void>(object, all.data());
				ending::apply_pose(object, rebuilt);
				return;
			}
			if (!in_hook && alive.load(std::memory_order_relaxed) && object && requested &&
			    scripted_arms::owns(object))
			{
				if (!scripted_arms::wants_pose(object, requested))
				{
					original();
					return;
				}
				in_hook = true;
				const auto leave = gsl::finally([] { in_hook = false; });
				alignas(16) std::array<std::uint32_t, 8> all{};
				std::copy_n(requested, 8, all.begin());
				bool rebuilt = false;
				for (unsigned b = 0; b < object->bone_count; ++b)
					all[b / 32] |= 0x80000000u >> (b % 32);
				for (unsigned w = 0; w < all.size(); ++w)
					if ((object->calculated[w] & all[w]) != all[w])
						rebuilt = true;
				calc_skeleton_hook.invoke<void>(object, all.data());
				scripted_arms::apply(object, rebuilt);
				return;
			}
			if (!in_hook && alive.load(std::memory_order_relaxed) && object && requested &&
			    mounted::owns_model(object))
			{
				in_hook = true;
				const auto leave = gsl::finally([] { in_hook = false; });
				alignas(16) std::array<std::uint32_t, 8> all{};
				std::copy_n(requested, 8, all.begin());
				bool rebuilt = false;
				for (unsigned b = 0; b < object->bone_count; ++b)
					all[b / 32] |= 0x80000000u >> (b % 32);
				for (unsigned word = 0; word < all.size(); ++word)
					if ((object->calculated[word] & all[word]) != all[word])
						rebuilt = true;
				calc_skeleton_hook.invoke<void>(object, all.data());
				mounted::apply_pose(object, rebuilt);
				return;
			}
			const bool empty = empty_native::owns(object);
			const bool independent = owned_native::owns(object);
			if (in_hook || !alive.load(std::memory_order_relaxed) || !object || !requested ||
			    (!empty && !independent &&
			     object !=
			         utils::hook::invoke<object_layout*>(client_object_address, primary_viewmodel_handle, 0)))
			{
				original();
				return;
			}
			if (!empty && !independent && owned_native::active())
			{
				original();
				return;
			}
			if (!scripted_control::predicted_allowed() &&
			    !(empty && (vehicles::presentation_allowed() || cliffhanger_physical::independent_hands() ||
			                sequences::independent_hands(game::CG_GetPredictedPlayerState(0)))))
			{
				original();
				return;
			}
			in_hook = true;
			const auto leave = gsl::finally([] { in_hook = false; });
			// The caller already owns the native DObj lock. This lock only serializes
			// the independent per-object solver contexts; no native call from another
			// component waits on this lock.
			const std::lock_guard lock(mutex);
			selected_solver = empty ? &empty_solver : &native_solver;
			if (independent)
			{
				for (auto& context : owned_solvers)
					if (context.last_object == object)
					{
						selected_solver = &context;
						break;
					}
				if (selected_solver == &native_solver)
					for (auto& context : owned_solvers)
						if (!context.last_object)
						{
							selected_solver = &context;
							break;
						}
			}
			++calls;
			std::array<game::XModel*, 32> models{};
			if (object->models && object->model_count <= models.size())
				std::copy_n(object->models, object->model_count, models.begin());
			// Latch a rejection as well as a success for this native skeleton epoch.
			// Tracking recovery between partial queries must not change one eye only.
			// A recreated hands-only DObj reuses its storage; its lifetime is also
			// part of the key, even if the native arena and epoch were recycled.
			const auto resource_generation = empty         ? empty_native::resource_generation()
			                                 : independent ? owned_native::resource_generation(object)
			                                               : 0;
			const auto assets = asset_generation.load(std::memory_order_relaxed);
			if ((independent && solver().last_resource_generation != resource_generation) ||
			    (solver().last_asset_generation && solver().last_asset_generation != assets))
				solver() = {};
			const auto remember = gsl::finally(
			    [&]
			    {
				    solver().last_object = object;
				    solver().last_matrices = object->matrices;
				    solver().last_timestamp = object->timestamp;
				    solver().last_resource_generation = resource_generation;
				    solver().last_asset_generation = assets;
				    solver().last_models = models;
				    solver().last_duplicate_parts = object->duplicate_parts;
				    solver().last_model_count = object->model_count;
				    solver().last_bone_count = object->bone_count;
				    std::copy_n(object->calculated, 8, solver().last_bits.begin());
			    });
			if (object == solver().last_object && object->matrices == solver().last_matrices &&
			    object->timestamp == solver().last_timestamp &&
			    resource_generation == solver().last_resource_generation &&
			    assets == solver().last_asset_generation && models == solver().last_models &&
			    solver().last_duplicate_parts == object->duplicate_parts &&
			    solver().last_model_count == object->model_count &&
			    solver().last_bone_count == object->bone_count &&
			    std::equal(solver().last_bits.begin(), solver().last_bits.end(), object->calculated))
			{
				original();
				return;
			}
			const auto stored = independent ? weapons::carry::stored_scene(owned_native::identity(object))
			                                : weapons::carry::storage_scene{};
			if (stored.item.id)
			{
				if (pose_stored(object, stored, resource_generation, assets))
				{
					++applications;
					reason = "stored weapon skeleton applied";
				}
				else
				{
					++skipped;
					reason = "stored weapon pose contract rejected";
					original();
				}
				return;
			}
			bool published_muzzle{};
			if (object != solver().last_object || resource_generation != solver().last_resource_generation ||
			    models != solver().last_models)
			{
				solver().twist.reset();
				solver().ads_transition.reset();
			}
			bool twist_committed{};
			const auto clear_twist = gsl::finally(
			    [&]
			    {
				    if (!twist_committed)
					    solver().twist.reset();
			    });
			const auto clear_muzzle = gsl::finally(
			    [&]
			    {
				    if (!published_muzzle)
				    {
					    solver().ads_transition.reset();
					    solver().ads_approach_meters = 0;
				    }
				    if (!published_muzzle &&
				        weapon_render_pose::drives_native_muzzle(
				            independent, owned_native::owner(object).id(), weapons::current_hold().id()) &&
				        (!empty || !weapons::current_hold().weapon))
				    {
					    weapons::invalidate_muzzle();
					    solver().grip_presenter.reset();
					    solver().secondary_chain.reset();
					    const auto current = weapons::current_hold();
					    if (!weapons::carry::active() && current.support != vr::hand::none)
						    weapons::commit_grip(current, weapons::grip_role::support, vr::hand::none);
				    }
			    });
			if (!enabled || !enabled->current.enabled)
			{
				reason = "disabled";
				++skipped;
				original();
				return;
			}
			probe = {};
			auto owner = model_pose_owner(
			    independent ? owned_native::owner(object) : weapons::sample_native_equipped(),
			    empty,
			    vehicles::presentation_allowed() || cliffhanger_physical::independent_hands() ||
			        sequences::independent_hands(game::CG_GetPredictedPlayerState(0)));
			probe.owner = owner;
			bool selection_transition = false;
			if (empty ? owner.weapon != 0
			          : (!owner.weapon || (!owner.can_fire() && !vr::valid_hand(owner.support))))
			{
				reason = "rear-grip owner unavailable";
				++skipped;
				original();
				return;
			}
			// Conservative single-held boundary: a second native viewmodel may be
			// dual wield or a special prop. Do not drive just one half as success.
			if (!empty && !independent &&
			    utils::hook::invoke<object_layout*>(client_object_address, primary_viewmodel_handle + 1, 0))
			{
				reason = "secondary viewmodel present; dedicated handling required";
				++skipped;
				original();
				return;
			}
			const auto input = controller_input::latest();
			head_pose_bridge::spatial_frame spatial{};
			const auto now = controller_input::clock::now();
			probe.input_sequence = input.sequence;
			probe.input_age_ms = std::chrono::duration<double, std::milli>(now - input.sampled_at).count();
			if (!input.focused || input.sequence == 0 || now < input.sampled_at ||
			    now - input.sampled_at > std::chrono::milliseconds(150) ||
			    !head_pose_bridge::get_spatial_frame(spatial) ||
			    spatial.generation != input.reference_generation)
			{
				reason = "tracking/spatial frame unavailable";
				++skipped;
				original();
				return;
			}
			probe.camera_age_ms =
			    std::chrono::duration<double, std::milli>(now - spatial.captured_at).count();
			rig layout{};
			if (!build_rig(*object, layout, resource_generation, assets, empty))
			{
				++skipped;
				original();
				return;
			}
			std::array<anchor, 2> targets{};
			if (!empty && !independent && weapons::carry::active() &&
			    !matches_weapon_model(*object, layout, owner.weapon))
			{
				// Selection intent changes before the native DObj. The old gun still
				// belongs to its original hand during this interval. Solve that actual
				// assembly instead of exposing the flat-screen switch animation or
				// assigning its contact/muzzle data to the newly acquired weapon.
				bool matched = false;
				for (const auto& held : weapons::carry::held_instances())
					if (held.id && matches_weapon_model(*object, layout, held.id.weapon))
					{
						owner = held.owner;
						matched = true;
						break;
					}
				if (!matched)
				{
					reason = "native receiver has no held owner";
					++skipped;
					original();
					return;
				}
				selection_transition = true;
				probe.owner = owner;
			}
			if (!empty && !independent && weapons::carry::active() &&
			    owner.weapon != vr::h2::sp::weapon_selection_request.read())
				selection_transition = true;
			const auto* render_view = *reinterpret_cast<const std::byte* const*>(0x141E39D30);
			if (!render_view)
			{
				reason = "render view unavailable";
				++skipped;
				original();
				return;
			}
			vec view_offset{};
			std::memcpy(view_offset.data(), render_view + 0x58, sizeof(view_offset));
			probe.offsets = {shoulder_half_width->current.value,
			                 shoulder_down->current.value,
			                 shoulder_back->current.value};
			probe.head_position = sub(spatial.head_position, view_offset);
			std::array<vec, 2> shoulders{};
			if (!make_shoulders(spatial, view_offset, probe.offsets, shoulders))
			{
				reason = "head/shoulder anchor contract rejected";
				++skipped;
				original();
				return;
			}
			probe.shoulder_target = shoulders;
			probe.wrist_offsets = {input.position_offsets_meters[0],input.position_offsets_meters[1],input.position_offsets_meters[2]};
			for (int hand = 0; hand < 2; ++hand)
			{
				head_pose_bridge::world_pose grip{}, aim{};
				if (!input.grip[hand].valid || !input.aim[hand].valid ||
				    !head_pose_bridge::tracking_to_world(spatial, input.grip[hand].tracking, grip) ||
				    !head_pose_bridge::tracking_to_world(spatial, input.aim[hand].tracking, aim))
				{
					if (independent && hand != int(owner.holding_hand()))
					{
						targets[hand] = {add(shoulders[hand], vec{0, 0, -.35f * spatial.units_per_meter}),
						                 {0, 0, 0, 1}};
						probe.target[hand] = targets[hand].position;
						continue;
					}
					reason = "both controller poses required";
					++skipped;
					original();
					return;
				}
				if (!tracked_wrist(input, spatial, view_offset, hand, probe.wrist_offsets, targets[hand]))
				{
					reason = "controller wrist offset rejected";
					++skipped;
					original();
					return;
				}
				probe.target[hand] = targets[hand].position;
			}
			// Native callers request partial bones (muzzle queries, rendering, etc.).
			// Complete the skeleton once before applying world-space IK; otherwise a
			// later child calculation would inherit an already corrected parent twice.
			alignas(16) std::array<std::uint32_t, 8> all{};
			std::copy_n(requested, 8, all.begin());
			for (int i = 0; i < layout.count; ++i)
				all[i / 32] |= 0x80000000u >> (i % 32);
			calc_skeleton_hook.invoke<void>(object, all.data());
			if (!object->matrices)
			{
				reason = "skeleton allocation unavailable";
				++skipped;
				return;
			}
			for (size_t i = 0; i < all.size(); ++i)
				if ((object->calculated[i] & all[i]) != all[i])
				{
					reason = "incomplete skeleton rejected";
					++skipped;
					return;
				}
			if (independent)
				for (int i = 0; i < layout.count; ++i)
				{
					object->matrices[i] = solver().binding.empty_bind[i];
					object->matrices[i].position =
					    add(sub(object->matrices[i].position, solver().binding.empty_head_anchor),
					        probe.head_position);
				}
			std::array<bone, 256> solved{};
			auto storage_body = spatial.body;
			if (storage_body.valid)
				storage_body.position = sub(storage_body.position, view_offset);
			probe.storage_body = storage_body;
			const auto finalize_arms = [&](unsigned visible = 3)
			{
				ladders::present(solver().binding.equipment_parts,
				                 layout,
				                 input,
				                 shoulders,
				                 spatial.head_yaw_axis,
				                 view_offset,
				                 {solved.data(), size_t(layout.count)},
				                 visible);
				const unsigned tracked = (input.grip[0].valid && input.aim[0].valid ? 1u : 0u) |
				                         (input.grip[1].valid && input.aim[1].valid ? 2u : 0u);
				probe.forearm_twist_hands = solver().twist.update(solver().binding.twist_binding,
				                                                  layout,
				                                                  {solved.data(), size_t(layout.count)},
				                                                  input.reference_generation,
				                                                  input.sampled_at,
				                                                  tracked);
				twist_committed = true;
			};
			for (int hand = 0; hand < 2; ++hand)
			{
				probe.native_wrist[hand] = object->matrices[layout.arms[hand].wrist].position;
				probe.native_shoulder[hand] = object->matrices[layout.arms[hand].shoulder].position;
				probe.native_elbow[hand] = object->matrices[layout.arms[hand].elbow].position;
			}
			std::array<bool, 2> limited{};
			if (empty)
			{
				vehicles::constrain_hands(
				    targets, solver().binding.equipment_parts.basis, view_offset, input.reference_generation);
				cliffhanger_physical::constrain_hands(
				    targets, solver().binding.equipment_parts.basis, view_offset, input.reference_generation);
				ending::constrain_hands(
				    targets, solver().binding.equipment_parts.basis, view_offset, input.reference_generation);
			}
			const auto equipment_targets = targets;
			unsigned mechanical_hands{};
			weapons::optics::view optic_view;
			vec ads_translation{};
			float ads_sight_to_muzzle_meters{};
			std::array<equipment::knife_hand_pose, 2> knife_poses{};
			if (empty)
			{
				for (int h = 0; h < 2; ++h)
					targets[h].rotation =
					    normalize(multiply(targets[h].rotation, solver().binding.empty_wrist_basis[h]));
				// A native tree-less DObj does not supply articulated translations.
				// Seed the model's own complete bind pose, rebased at the current
				// head/view anchor, rather than inventing an animation or old weapon.
				auto rest = solver().binding.empty_bind;
				for (int i = 0; i < layout.count; ++i)
					rest[i].position =
					    add(sub(rest[i].position, solver().binding.empty_head_anchor), probe.head_position);
				if (!solve_arms(layout,
				                {rest.data(), size_t(layout.count)},
				                targets,
				                shoulders,
				                spatial.head_yaw_axis,
				                {solved.data(), size_t(layout.count)},
				                limited))
				{
					reason = "empty-hand arm contract rejected";
					++skipped;
					return;
				}
				std::array<std::uint64_t, 2> reload_tokens{};
				const auto interaction_presentation =
				    hands::present_interactions(solver().binding.equipment_parts,
				                                {.skeleton = layout,
				                                 .controllers = input,
				                                 .targets = equipment_targets,
				                                 .shoulders = shoulders,
				                                 .axes = spatial.head_yaw_axis,
				                                 .units_per_meter = spatial.units_per_meter,
				                                 .solved = {solved.data(), std::size_t(layout.count)},
				                                 .posed_hands = 0,
				                                 .visible_hands = 3});
				reload_tokens = interaction_presentation.reload_item_tokens;
				finalize_arms();
				std::copy_n(solved.begin(), layout.count, object->matrices);
				attachments::publish(
				    {reinterpret_cast<std::uintptr_t>(object),
				     reinterpret_cast<std::uintptr_t>(object->matrices),
				     object->timestamp,
				     {unsigned(layout.arms[0].wrist), unsigned(layout.arms[1].wrist)},
				     {object->matrices[layout.arms[0].wrist], object->matrices[layout.arms[1].wrist]},
				     input.reference_generation,
				     input.sequence,
				     input.sampled_at,
				     interaction_presentation.knife_revision,
				     probe.head_position,
				     spatial.head_yaw_axis,
				     spatial.units_per_meter,
				     {solver().binding.equipment_parts.library.mirror_basis[layout.arms[0].wrist],
				      solver().binding.equipment_parts.library.mirror_basis[layout.arms[1].wrist]},
				     {},
				     storage_body,
				     reload_tokens});
				empty_native::accept(object, object->matrices, object->timestamp);
				vehicles::publish_render_hands(
				    input,
				    {{hands::pose_math::as_anchor(object->matrices[layout.arms[0].wrist]),
				      hands::pose_math::as_anchor(object->matrices[layout.arms[1].wrist])}},
				    view_offset);
				cliffhanger_physical::publish_hands(
				    input,
				    {{hands::pose_math::as_anchor(object->matrices[layout.arms[0].wrist]),
				      hands::pose_math::as_anchor(object->matrices[layout.arms[1].wrist])}},
				    view_offset);
				for (int h = 0; h < 2; ++h)
				{
					probe.solved_wrist[h] = solved[layout.arms[h].wrist].position;
					probe.solved_shoulder[h] = solved[layout.arms[h].shoulder].position;
					probe.solved_elbow[h] = solved[layout.arms[h].elbow].position;
				}
				probe.solved = true;
				++applications;
				limited_left += limited[0];
				limited_right += limited[1];
				reason = "independent empty hands applied";
				return;
			}
			const bool profiled =
			    grips_enabled && grips_enabled->current.enabled && solver().binding.active_profile;
			if (solver().binding.active_profile && solver().binding.active_profile->defense &&
			    (!profiled || !weapons::shield::enabled()))
			{
				reason = "shield requires enabled physical grip and defense adapters";
				++skipped;
				return;
			}
			if (solver().binding.active_profile && solver().binding.active_profile->melee && !profiled)
			{
				reason = "knife requires its physical grip adapter";
				++skipped;
				return;
			}
			weapons::model_anchor control_grip{};
			if (profiled)
			{
				weapons::carry::pose_profile carry_profile(
				    *solver().binding.active_profile,
				    owner,
				    layout,
				    solver().binding.pose_library_data,
				    weapons::carry::support_basis(owner, input.reference_generation));
				const auto ordinary_supports = carry_profile.supports;
				auto posing =
				    weapons::carry::active() ? carry_profile.value : *solver().binding.active_profile;
				const auto* paused = game::Dvar_FindVar("cl_paused");
				const bool gameplay = game::CL_IsCgameInitialized() && *game::keyCatchers == 0 && paused &&
				                      paused->current.integer == 0;
				auto module_grip = weapons::underbarrel::current(owner.id());
				module_grip.travel = weapons::underbarrel::displayed_travel(module_grip,
				                                                            owner,
				                                                            input,
				                                                            targets,
				                                                            spatial.units_per_meter,
				                                                            solver().binding.assembly_key,
				                                                            gameplay && !selection_transition,
				                                                            now);
				if (solver().binding.underbarrel_parts && vr::valid_hand(owner.holding_hand()) &&
				    module_grip.owns_support)
				{
					const int off = owner.can_fire() ? 1 - int(owner.rear) : int(owner.support);
					const auto wrist =
					    weapons::underbarrel::support_anchor(solver().binding.underbarrel_parts,
					                                         layout,
					                                         solver().binding.pose_library_data,
					                                         ordinary_supports[off],
					                                         off,
					                                         module_grip,
					                                         spatial.units_per_meter);
					posing.wrists[off] = wrist;
					carry_profile.supports[off] = wrist;
					posing.free_hand_reference = solver().binding.active_profile->free_hand_reference
					                                 ? solver().binding.active_profile->free_hand_reference
					                                 : &solver().binding.active_profile->wrists;
				}
				else if (solver().binding.underbarrel_parts && module_grip.active && owner.can_fire() &&
				         module_grip.travel > 0)
				{
					const int off = 1 - int(owner.rear);
					auto moving = module_grip;
					moving.support_role = weapons::underbarrel::lease::action;
					const auto wrist =
					    weapons::underbarrel::support_anchor(solver().binding.underbarrel_parts,
					                                         layout,
					                                         solver().binding.pose_library_data,
					                                         ordinary_supports[off],
					                                         off,
					                                         moving,
					                                         spatial.units_per_meter);
					posing.wrists[off] = wrist;
					carry_profile.supports[off] = wrist;
					posing.free_hand_reference = solver().binding.active_profile->free_hand_reference
					                                 ? solver().binding.active_profile->free_hand_reference
					                                 : &solver().binding.active_profile->wrists;
				}
				const auto support_plan = hand_interaction::pose(owner.support);
				if (support_plan.driver &&
				    !hand_interaction::has(support_plan.abilities, hand_interaction::capability::aim))
					posing.aiming = weapons::aim_rule::rear_hand;
				if (posing.tube && weapons::tube::pumped(posing.tube->ammunition))
				{
					const auto pump = weapons::tube::current(owner.id());
					const auto movement = scale(posing.tube->interaction.rack.slide_axis,
					                            pump.travel * spatial.units_per_meter);
					for (auto& support : carry_profile.supports)
						support.position = add(support.position, movement);
					const int off = 1 - posing.authored_rear;
					posing.wrists[off].position = add(posing.wrists[off].position, movement);
					posing.free_hand_reference = solver().binding.active_profile->free_hand_reference
					                                 ? solver().binding.active_profile->free_hand_reference
					                                 : &solver().binding.active_profile->wrists;
				}
				const auto& pose_owner = weapons::carry::active() ? carry_profile.solver_owner : owner;
				const auto reload = solver().binding.mechanical_parts.valid
				                        ? weapons::physical_reload::current(owner.id())
				                        : weapons::physical_reload::presentation{};
				const bool knife_held = equipment::held_by(owner.manipulation_hand());
				// A co-grasp keeps the equipment wrist basis through part manipulation
				// and knife return. Geometry and the final hand solve use this same basis.
				auto free_wrists = posing.free_hand_reference ? *posing.free_hand_reference : posing.wrists;
				const auto ordinary_wrists = free_wrists;
				if (posing.reload && reload.active && !reload.fault && reload.magazine_leased() &&
				    posing.reload->magazine_tracking == weapons::magazine_tracking_frame::controller &&
				    !knife_held && !reload.use_knife_magazine_grasp(false) &&
				    vr::valid_hand(owner.manipulation_hand()))
				{
					const int off = static_cast<int>(owner.manipulation_hand());
					const auto mirror =
					    solver().binding.pose_library_data.mirror_basis[layout.arms[off].wrist];
					const auto selected = weapons::select_magazine_grip(
					    *posing.reload, {0, 0, 0, 1}, off, mirror, false, true, reload.magazine_pose);
					free_wrists[off].rotation = weapons::magazine_wrist_basis(
					    *posing.reload, selected, ordinary_wrists[off].rotation, false);
					posing.free_hand_reference = &free_wrists;
				}
				if (posing.reload &&
				    (posing.reload->knife_magazine_in_wrist || !posing.reload->knife_slide_grips.empty()) &&
				    solver().binding.equipment_parts.valid && vr::valid_hand(owner.manipulation_hand()))
				{
					const auto off = static_cast<int>(owner.manipulation_hand());
					if (knife_held || reload.use_knife_magazine_grasp(false) ||
					    reload.use_knife_slide_grasp(false))
					{
						free_wrists[off].rotation = solver().binding.equipment_parts.basis[off];
						posing.free_hand_reference = &free_wrists;
					}
				}
				const auto animation = independent ? weapons::animation_presentation{true, true}
				                                   : weapons::sample_animation_presentation(
				                                         object->tree, *solver().binding.active_profile);
				if (!animation.valid || !solver().binding.pose_library_data.valid)
				{
					reason = "profile animation/hand-pose schema unavailable";
					++skipped;
					return;
				}
				probe.grip = solver().grip_presenter.update(
				    posing,
				    solver().binding.pose_library_data,
				    layout,
				    {object->matrices, static_cast<size_t>(layout.count)},
				    targets,
				    shoulders,
				    spatial.head_yaw_axis,
				    input,
				    pose_owner,
				    solver().binding.assembly_key,
				    spatial.units_per_meter,
				    gameplay,
				    animation.equip,
				    now,
				    {solved.data(), static_cast<size_t>(layout.count)},
				    limited,
				    [&]
				    {
					    if (!owner.can_fire())
						    return false;
					    const auto other = static_cast<vr::hand>(1 - static_cast<int>(owner.rear));
					    if (module_grip.active && !module_grip.owns_support &&
					        (module_grip.ammo.open || module_grip.travel > .001f))
						    return false;
					    if (weapons::underbarrel::enabled() &&
					        weapons::underbarrel::directional_grips(
					            solver().binding.underbarrel_parts.type) &&
					        !module_grip.owns_support)
					    {
						    const int off = int(other);
						    const auto wrist =
						        multiply(conjugate(targets[int(owner.rear)].rotation), targets[off].rotation);
						    if (!weapons::underbarrel::support_facing(
						            weapons::underbarrel::controller_facing(wrist, off),
						            owner.support == other,
						            solver().binding.underbarrel_parts.type))
							    return false;
					    }
					    if (!weapons::carry::hand_available(other) && owner.support != other)
						    return false;
					    const auto revolver = weapons::cylinder::current(owner.id());
					    const auto hinged = weapons::break_action::current(owner.id());
					    if (hinged.active)
						    return hinged.ammo.phase == weapons::break_action::action::closed &&
						           !hinged.barrel_held && hinged.ammo.loader_hand == vr::hand::none;
					    const auto shotgun = weapons::tube::current(owner.id());
					    if (shotgun.active && weapons::lever::blocks_support(shotgun.lever))
						    return false;
					    if (shotgun.active)
						    return weapons::physical_reload::support_available(
						        input,
						        owner,
						        (shotgun.rack_held &&
						         !weapons::tube::pumped(shotgun.definition->ammunition)) ||
						            shotgun.ammo.loader_hand != vr::hand::none);
					    if (revolver.active)
						    return weapons::physical_reload::support_available(
						        input, owner, revolver.ammo.loader_hand != vr::hand::none);
					    return weapons::physical_reload::support_available(input, owner);
				    }(),
				    weapons::carry::active());
				if (!probe.grip.valid)
				{
					reason = "profile grip/pose contract rejected";
					++skipped;
					return;
				}
				if (gameplay && !selection_transition)
					weapons::recoil::apply_to_pose(
					    layout,
					    {solved.data(), size_t(layout.count)},
					    int(owner.rear),
					    int(probe.grip.support),
					    spatial.head_yaw_axis,
					    weapons::recoil::current_climb(owner, input.reference_generation, now));
				const auto ads_control = weapons::optics::control_for(owner, input);
				if (layout.muzzle >= 0)
				{
					const auto ads_rotation = normalize(solved[layout.muzzle].rotation);
					const std::array<vec, 3> ads_axis{rotate(ads_rotation, {1, 0, 0}),
					                                  rotate(ads_rotation, {0, 1, 0}),
					                                  rotate(ads_rotation, {0, 0, 1})};
					// Measure the current unassisted solve. Native ADS still dwells, but
					// approach follows angle and eye distance before its button activates.
					const auto raised = weapons::ads_alignment::measure(probe.head_position,
					                                                    spatial.head_forward,
					                                                    solved[layout.muzzle].position,
					                                                    ads_axis,
					                                                    spatial.units_per_meter);
					const auto& sight = solver().binding.ads_sight;
					const auto eye_geometry =
					    weapons::ads_comfort::measure_eye(sight,
					                                      {solved.data(), size_t(layout.count)},
					                                      probe.head_position,
					                                      ads_axis[0],
					                                      spatial.units_per_meter);
					if (raised.valid && eye_geometry.valid)
						ads_sight_to_muzzle_meters = std::max(0.f, raised.depth - eye_geometry.depth_meters);
					const float alignment =
					    weapons::ads_alignment::approach(raised, ads_sight_to_muzzle_meters);
					const float distance_weight = eye_geometry.valid
					                                  ? weapons::ads_comfort::distance_weight(
					                                        sight.optic.type, eye_geometry.distance_meters)
					                                  : 0.f;
					const auto comfort_plan = hand_interaction::pose(owner.support);
					const bool manipulating =
					    reload.part_leased() ||
					    (comfort_plan.driver && comfort_plan.driver.object == owner.id() &&
					     (hand_interaction::has(comfort_plan.abilities,
					                            hand_interaction::capability::action) ||
					      !hand_interaction::has(comfort_plan.abilities, hand_interaction::capability::aim)));
					// Fade the target, not the hard clearance cap: extending the sight
					// must ease back through the same filter as angular/grip changes.
					const float approach = solver().ads_transition.update(
					    input,
					    owner,
					    solver().binding.assembly_key,
					    sight.optic.type,
					    ads_control.allowed && solver().binding.active_profile->id != "javelin" && gameplay &&
					        !selection_transition && !manipulating,
					    alignment * distance_weight,
					    eye_geometry.clearance_meters,
					    now);
					const auto translation = scale(ads_axis[0], -approach * spatial.units_per_meter);
					solver().ads_approach_meters = 0;
					// Run before part presentation freezes inserted magazines/partitioned
					// meshes: those scene attachments must capture the same moved gun.
					if (weapons::ads_comfort::apply(layout,
					                                {solved.data(), size_t(layout.count)},
					                                int(owner.rear),
					                                int(probe.grip.support),
					                                spatial.head_yaw_axis,
					                                translation))
					{
						ads_translation = translation;
						solver().ads_approach_meters = approach;
					}
				}
				else
				{
					solver().ads_transition.reset();
					solver().ads_approach_meters = 0;
				}
				probe.equip_suppressed = animation.equip;
				weapons::part_presentation::result mechanical_pose{};
				const auto manipulating_hand = owner.manipulation_hand();
				const bool manipulation =
				    gameplay && !selection_transition && vr::valid_hand(manipulating_hand) &&
				    hand_interaction::permits(manipulating_hand,
				                              posing.launcher   ? hand_interaction::domain::launcher
				                              : posing.reload   ? hand_interaction::domain::magazine
				                              : posing.cylinder ? hand_interaction::domain::cylinder
				                              : posing.tube     ? hand_interaction::domain::tube
				                                                : hand_interaction::domain::hinge,
				                              owner.id(),
				                              posing.reload && posing.reload->knife_magazine_in_wrist
				                                  ? hand_interaction::recipe::knife_magazine
				                                  : hand_interaction::recipe::single,
				                              hand_interaction::role::supply);
				part_hand_frame hand_motion(solver().part_motion,
				                            layout,
				                            input,
				                            owner,
				                            targets,
				                            shoulders,
				                            spatial.head_yaw_axis,
				                            {solved.data(), size_t(layout.count)},
				                            solver().binding.assembly_key,
				                            now,
				                            spatial.units_per_meter,
				                            gameplay && !selection_transition);
				const auto secondary_pose =
				    weapons::underbarrel::present(solver().binding.underbarrel_parts,
				                                  layout,
				                                  solver().binding.pose_library_data,
				                                  posing,
				                                  ordinary_supports,
				                                  input,
				                                  owner,
				                                  module_grip,
				                                  solver().binding.assembly_key,
				                                  gameplay && !selection_transition,
				                                  targets,
				                                  shoulders,
				                                  spatial.head_yaw_axis,
				                                  probe.head_position,
				                                  view_offset,
				                                  spatial.units_per_meter,
				                                  {solved.data(), size_t(layout.count)},
				                                  &hand_motion);
				// New-pinch arbitration runs in the server controller. A render-time
				// modifier must not cancel an already held primary magazine/handle.
				const bool primary_manipulation = manipulation;
				const int reload_off =
				    vr::valid_hand(owner.holding_hand()) ? 1 - int(owner.holding_hand()) : 0;
				if (solver().binding.mechanical_parts.valid)
					mechanical_pose = weapons::physical_reload::present(
					    object,
					    object->timestamp,
					    object->matrices,
					    solver().binding.mechanical_parts,
					    layout,
					    solver().binding.pose_library_data,
					    posing,
					    input,
					    owner,
					    reload,
					    knife_held,
					    solver().binding.assembly_key,
					    gameplay && !selection_transition,
					    primary_manipulation,
					    targets,
					    shoulders,
					    spatial.head_yaw_axis,
					    probe.head_position,
					    view_offset,
					    spatial.units_per_meter,
					    {solved.data(), static_cast<size_t>(layout.count)},
					    now,
					    ordinary_wrists[reload_off].rotation,
					    solver().binding.equipment_parts.valid
					        ? std::optional{solver().binding.equipment_parts.basis[reload_off]}
					        : std::nullopt,
					    spatial.body,
					    &hand_motion);
				else if (solver().binding.launcher_parts.valid)
					mechanical_pose = weapons::launcher::present(object,
					                                             object->timestamp,
					                                             object->matrices,
					                                             solver().binding.launcher_parts,
					                                             layout,
					                                             solver().binding.pose_library_data,
					                                             posing,
					                                             input,
					                                             owner,
					                                             solver().binding.assembly_key,
					                                             gameplay && !selection_transition,
					                                             manipulation,
					                                             targets,
					                                             view_offset,
					                                             spatial.units_per_meter,
					                                             {solved.data(), size_t(layout.count)},
					                                             &hand_motion);
				else if (solver().binding.break_parts.valid)
					mechanical_pose = weapons::break_action::present(object,
					                                                 object->timestamp,
					                                                 object->matrices,
					                                                 solver().binding.break_parts,
					                                                 layout,
					                                                 solver().binding.pose_library_data,
					                                                 posing,
					                                                 input,
					                                                 owner,
					                                                 solver().binding.assembly_key,
					                                                 gameplay && !selection_transition,
					                                                 manipulation,
					                                                 targets,
					                                                 shoulders,
					                                                 spatial.head_yaw_axis,
					                                                 probe.head_position,
					                                                 view_offset,
					                                                 spatial.units_per_meter,
					                                                 {solved.data(), size_t(layout.count)},
					                                                 now,
					                                                 &hand_motion);
				else if (solver().binding.cylinder_parts.valid)
					mechanical_pose =
					    weapons::cylinder::present(object,
					                               object->timestamp,
					                               object->matrices,
					                               solver().binding.cylinder_parts,
					                               layout,
					                               solver().binding.pose_library_data,
					                               posing,
					                               input,
					                               owner,
					                               solver().binding.assembly_key,
					                               gameplay && !selection_transition,
					                               manipulation,
					                               targets,
					                               shoulders,
					                               spatial.head_yaw_axis,
					                               probe.head_position,
					                               view_offset,
					                               spatial.units_per_meter,
					                               {solved.data(), static_cast<size_t>(layout.count)},
					                               now,
					                               &hand_motion);
				else if (solver().binding.tube_parts.valid)
					mechanical_pose = weapons::tube::present(object,
					                                         object->timestamp,
					                                         object->matrices,
					                                         solver().binding.tube_parts,
					                                         layout,
					                                         solver().binding.pose_library_data,
					                                         posing,
					                                         input,
					                                         owner,
					                                         solver().binding.assembly_key,
					                                         gameplay && !selection_transition,
					                                         manipulation,
					                                         targets,
					                                         shoulders,
					                                         spatial.head_yaw_axis,
					                                         probe.head_position,
					                                         view_offset,
					                                         spatial.units_per_meter,
					                                         {solved.data(), size_t(layout.count)},
					                                         now,
					                                         &hand_motion);
				weapons::heartbeat::present(solver().binding.heartbeat_parts,
				                            layout,
				                            solver().binding.pose_library_data,
				                            posing,
				                            input,
				                            owner,
				                            solver().binding.assembly_key,
				                            targets,
				                            shoulders,
				                            spatial.head_yaw_axis,
				                            view_offset,
				                            spatial.units_per_meter,
				                            {solved.data(), size_t(layout.count)},
				                            &hand_motion);
				hand_motion.finish();
				if (secondary_pose.valid)
				{
					mechanical_pose.hidden =
					    weapons::combine_part_masks(mechanical_pose.hidden, secondary_pose.hidden);
					mechanical_pose.valid = true;
					mechanical_pose.posed_hands |= secondary_pose.posed_hands;
				}
				mechanical_hands = mechanical_pose.posed_hands;
				knife_poses = mechanical_pose.knife_poses;
				// One final mask for this exact solved pose, including when reload is
				// inactive/unavailable or the weapon has no mechanical adapter at all.
				optic_view = weapons::optics::present(solver().binding.optic,
				                                      owner,
				                                      input,
				                                      {solved.data(), size_t(layout.count)},
				                                      view_offset,
				                                      spatial.units_per_meter,
				                                      ads_control.active);
				weapons::viewmodel_visibility::publish(
				    object,
				    object->matrices,
				    object->timestamp,
				    weapons::combine_part_masks(solver().binding.assembly_hidden,
				                                mechanical_pose.valid ? mechanical_pose.hidden
				                                                      : weapons::part_mask{}),
				    solver().binding.active_profile->viewmodel.visibility,
				    independent &&
				            weapons::carry::hand_has_weapon(
				                static_cast<vr::hand>(1 - int(owner.holding_hand()))) &&
				            owner.support == vr::hand::none
				        ? layout.arms[1 - int(owner.holding_hand())].shoulder
				        : -1,
				    optic_view.active ? solver().binding.optic.hidden
				                      : std::array<const game::XSurface*, 8>{});
				if (solver().binding.active_profile->secondary_motion)
					(void)solver().secondary_chain.update(*solver().binding.active_profile->secondary_motion,
					                                      solver().binding.secondary_parts,
					                                      layout,
					                                      {solved.data(), size_t(layout.count)},
					                                      view_offset,
					                                      spatial.units_per_meter,
					                                      solver().binding.assembly_key ^ owner.rear_revision,
					                                      input.reference_generation,
					                                      input.sequence,
					                                      input.sampled_at,
					                                      gameplay);
				else
					solver().secondary_chain.reset();
				const auto control = vr::valid_hand(owner.rear) ? owner.rear : owner.pose_rear;
				const auto& contact = carry_profile.controls[control == vr::hand::left ? 0 : 1];
				control_grip = {
				    true,
				    add(solved[layout.gun].position, rotate(solved[layout.gun].rotation, contact.position)),
				    view_offset};
				if (owner.can_fire())
				{
					const auto off = 1 - static_cast<int>(owner.rear);
					if (const auto* other = weapons::carry::held_profile(static_cast<vr::hand>(off)))
					{
						const auto grip = weapons::carry::control_grip(
						    *other,
						    off,
						    solver().binding.pose_library_data.mirror_basis[layout.arms[off].wrist]);
						const auto wrist = layout.arms[off].wrist;
						auto rotation = normalize(multiply(
						    targets[off].rotation,
						    weapons::carry::control_basis(
						        *other, grip, solver().binding.pose_library_data.neutral_wrists[off], off)));
						if (other->forearm)
						{
							const auto a = layout.arms[off];
							const auto mounted =
							    mount_forearm(*other->forearm,
							                  length(sub(object->matrices[a.elbow].position,
							                             object->matrices[a.shoulder].position)),
							                  length(sub(object->matrices[a.wrist].position,
							                             object->matrices[a.elbow].position)),
							                  {targets[off].position, rotation},
							                  shoulders[off],
							                  spatial.head_yaw_axis,
							                  off);
							if (mounted.limb.valid &&
							    apply_mounted_limb(layout,
							                       solved,
							                       off,
							                       mounted,
							                       shoulders[off],
							                       normalize(multiply(mounted.rotation, grip.rotation))))
								rotation = mounted.rotation;
						}
						hands::pose_math::move_part(
						    layout,
						    wrist,
						    {solved[wrist].position, normalize(multiply(rotation, grip.rotation))},
						    solved);
						hands::pose_mirror::fingers(layout,
						                            solver().binding.pose_library_data,
						                            posing,
						                            other->fingers,
						                            off,
						                            solved,
						                            off == 0 && !other->control_grips);
					}
				}
				const float safe_index = solver().trigger_finger.update(
				    input,
				    owner,
				    solver().binding.assembly_key,
				    gameplay && !selection_transition &&
				        trigger_discipline::eligible(
				            solver().binding.active_profile, owner, mechanical_hands));
				solver().safe_index_weight = safe_index;
				solver().safe_index_applied =
				    trigger_discipline::apply(layout,
				                              solver().binding.pose_library_data,
				                              posing,
				                              owner.rear,
				                              safe_index,
				                              {solved.data(), size_t(layout.count)});
				if (weapons::carry::active())
				{
					auto scene = weapons::carry::scene{
					    owner,
					    {add(solved[layout.gun].position, view_offset),
					     normalize(solved[layout.gun].rotation)},
					    carry_profile.controls,
					    owner.can_fire() ? probe.grip.support : vr::hand::none,
					    input.sequence,
					    input.reference_generation,
					    input.sampled_at,
					    carry_profile.supports,
					    layout.muzzle >= 0
					        ? hands::pose_math::compose(
					              hands::pose_math::inverse(hands::pose_math::as_anchor(solved[layout.gun])),
					              hands::pose_math::as_anchor(solved[layout.muzzle]))
					        : anchor{},
					    layout.muzzle >= 0,
					    solver().binding.active_profile,
					    independent,
					    solver().binding.ejection,
					    solver().binding.assembly_key};
					if (solver().binding.active_profile->forearm)
					{
						for (int h = 0; h < 2; ++h)
						{
							const auto a = layout.arms[h];
							scene.arm_lengths[h] = {length(sub(object->matrices[a.elbow].position,
							                                   object->matrices[a.shoulder].position)),
							                        length(sub(object->matrices[a.wrist].position,
							                                   object->matrices[a.elbow].position))};
						}
						scene.shoulder_dimensions = {probe.offsets.half_width_meters,
						                             probe.offsets.down_meters,
						                             probe.offsets.back_meters};
					}
					scene.firing_arm = capture_arm(
					    layout, solved, int(owner.holding_hand()), view_offset, spatial.units_per_meter);
					scene.control_rotations = carry_profile.rotations;
					if (vr::valid_hand(owner.support))
					{
						scene.support_controller = targets[int(owner.support)].rotation;
						scene.has_support_controller = true;
					}
					weapons::carry::publish_scene(scene);
				}
			}
			else if (!solve(layout,
			                {object->matrices, static_cast<size_t>(layout.count)},
			                targets,
			                shoulders,
			                spatial.head_yaw_axis,
			                static_cast<int>(vr::valid_hand(owner.rear) ? owner.rear : owner.support),
			                {solved.data(), static_cast<size_t>(layout.count)},
			                limited))
			{
				reason = "pose/IK contract rejected";
				++skipped;
				return;
			}
			if (!profiled && !selection_transition)
				weapons::recoil::apply_to_pose(
				    layout,
				    {solved.data(), size_t(layout.count)},
				    int(owner.rear),
				    -1,
				    spatial.head_yaw_axis,
				    weapons::recoil::current_climb(owner, input.reference_generation, now));
			if (!profiled)
			{
				// Capture the native rear contact before replacing the arm matrices.
				// A free right hand is not the grip of a left-held gun.
				auto contact =
				    hands::pose_math::compose(
				        hands::pose_math::inverse(hands::pose_math::as_anchor(object->matrices[layout.gun])),
				        hands::pose_math::as_anchor(object->matrices[layout.rear_grip_wrist]))
				        .position;
				if (owner.rear == vr::hand::left ||
				    (owner.rear == vr::hand::none && owner.pose_rear == vr::hand::left))
					contact[1] = -contact[1];
				control_grip = {
				    true,
				    add(solved[layout.gun].position, rotate(solved[layout.gun].rotation, contact)),
				    view_offset};
				if (independent)
					weapons::viewmodel_visibility::publish(
					    object,
					    object->matrices,
					    object->timestamp,
					    {},
					    weapons::part_visibility::surface,
					    weapons::carry::hand_has_weapon(
					        static_cast<vr::hand>(1 - int(owner.holding_hand()))) &&
					            owner.support == vr::hand::none
					        ? layout.arms[1 - int(owner.holding_hand())].shoulder
					        : -1);
				solver().grip_presenter.reset();
				solver().secondary_chain.reset();
				solver().ads_transition.reset();
				solver().ads_approach_meters = 0;
				solver().trigger_finger.reset();
				solver().safe_index_weight = 0;
				solver().safe_index_applied = false;
				if (weapons::carry::active())
				{
					const auto gun = hands::pose_math::as_anchor(solved[layout.gun]);
					std::array<anchor, 2> contacts;
					// A left-held unprofiled gun leaves the right arm free. Its solved
					// wrist is no longer the control grip! Capture the native rear-hand
					// relation before IK, then mirror that same local contact.
					const auto right = hands::pose_math::compose(
					    hands::pose_math::inverse(hands::pose_math::as_anchor(object->matrices[layout.gun])),
					    hands::pose_math::as_anchor(object->matrices[layout.rear_grip_wrist]));
					contacts[1] = right;
					contacts[0] = right;
					contacts[0].position[1] = -contacts[0].position[1];
					auto scene = weapons::carry::scene{
					    owner,
					    {add(gun.position, view_offset), gun.rotation},
					    contacts,
					    vr::hand::none,
					    input.sequence,
					    input.reference_generation,
					    input.sampled_at,
					    {},
					    hands::pose_math::compose(hands::pose_math::inverse(gun),
					                              hands::pose_math::as_anchor(solved[layout.muzzle])),
					    true,
					    nullptr,
					    independent};
					scene.firing_arm = capture_arm(
					    layout, solved, int(owner.holding_hand()), view_offset, spatial.units_per_meter);
					weapons::carry::publish_scene(scene);
				}
			}
			const auto support = profiled ? probe.grip.support : vr::hand::none;
			if (selection_transition)
			{
				// Visual continuity is not firing authority. Keep the two tracked
				// arms while waiting for the requested receiver; never publish the
				// previous gun as the new gun's native muzzle/skin identity.
				const auto live = weapons::carry::held(owner.id());
				if (live.revision != owner.revision || live.rear != owner.rear ||
				    live.support != owner.support)
				{
					reason = "transition grip authority changed";
					++skipped;
					return;
				}
				finalize_arms();
				std::copy_n(solved.begin(), layout.count, object->matrices);
				probe.solved = true;
				++applications;
				++transition_poses;
				for (int h = 0; h < 2; ++h)
					probe.solved_wrist[h] = solved[layout.arms[h].wrist].position;
				reason = "tracked hands retained during native selection";
				return;
			}
			weapons::muzzle_frame muzzle{};
			if (independent)
			{
				const auto live = owned_native::owner(object);
				if (live.id() != owner.id() || live.revision != owner.revision)
				{
					reason = "instance owner changed during solve";
					++skipped;
					return;
				}
			}
			if (layout.muzzle >= 0)
			{
				const auto muzzle_rotation = normalize(solved[layout.muzzle].rotation);
				muzzle = {true,
				          owner,
				          input.reference_generation,
				          input.sequence,
				          input.sampled_at,
				          spatial.captured_at,
				          add(solved[layout.muzzle].position, view_offset),
				          {rotate(muzzle_rotation, {1, 0, 0}),
				           rotate(muzzle_rotation, {0, 1, 0}),
				           rotate(muzzle_rotation, {0, 0, 1})},
				          profiled ? solver().binding.active_profile->id : std::string_view{},
				          spatial.units_per_meter,
				          {true, solved[layout.muzzle].position, view_offset},
				          !(profiled && solver().binding.active_profile->defense)};
				muzzle.head_position = spatial.head_position;
				muzzle.head_forward = spatial.head_forward;
				muzzle.optic = optic_view;
				muzzle.ads_translation = ads_translation;
				muzzle.ads_sight_to_muzzle_meters = ads_sight_to_muzzle_meters;
				if (layout.laser >= 0)
				{
					const auto rotation = normalize(solved[layout.laser].rotation);
					muzzle.laser = {true, solved[layout.laser].position, view_offset};
					muzzle.laser_axis = {rotate(rotation, {1, 0, 0}),
					                     rotate(rotation, {0, 1, 0}),
					                     rotate(rotation, {0, 0, 1})};
				}
				if (weapon_render_pose::drives_native_muzzle(
				        independent, owner.id(), weapons::current_hold().id()) &&
				    !weapons::publish_muzzle(owner, support, muzzle, owner))
				{
					reason = "grip authority changed during pose commit";
					++skipped;
					return;
				}
				if (!independent)
					muzzle = weapons::current_muzzle();
			}

			probe.owner = owner;
			unsigned visible_hands = 3;
			unsigned entity_hands = mechanical_hands;
			for (const auto h : {owner.rear, owner.support, support})
				if (vr::valid_hand(h))
					entity_hands |= 1u << unsigned(h);
			if (independent && vr::valid_hand(owner.holding_hand()) && owner.support == vr::hand::none)
			{
				const auto other = vr::hand(1 - int(owner.holding_hand()));
				if (weapons::carry::hand_has_weapon(other))
					visible_hands &= ~(1u << unsigned(other));
			}
			std::array<std::uint64_t, 2> reload_tokens{};
			const auto interaction_presentation =
			    hands::present_interactions(solver().binding.equipment_parts,
			                                {.skeleton = layout,
			                                 .controllers = input,
			                                 .targets = equipment_targets,
			                                 .shoulders = shoulders,
			                                 .axes = spatial.head_yaw_axis,
			                                 .units_per_meter = spatial.units_per_meter,
			                                 .solved = {solved.data(), std::size_t(layout.count)},
			                                 .posed_hands = entity_hands,
			                                 .visible_hands = visible_hands});
			reload_tokens = interaction_presentation.reload_item_tokens;
			finalize_arms(visible_hands);
			std::copy_n(solved.begin(), layout.count, object->matrices);
			attachments::publish(
			    {reinterpret_cast<std::uintptr_t>(object),
			     reinterpret_cast<std::uintptr_t>(object->matrices),
			     object->timestamp,
			     {unsigned(layout.arms[0].wrist), unsigned(layout.arms[1].wrist)},
			     {object->matrices[layout.arms[0].wrist], object->matrices[layout.arms[1].wrist]},
			     input.reference_generation,
			     input.sequence,
			     input.sampled_at,
			     interaction_presentation.knife_revision,
			     probe.head_position,
			     spatial.head_yaw_axis,
			     spatial.units_per_meter,
			     {solver().binding.equipment_parts.library.mirror_basis[layout.arms[0].wrist],
			      solver().binding.equipment_parts.library.mirror_basis[layout.arms[1].wrist]},
			     knife_poses,
			     storage_body,
			     reload_tokens});
			// Only AFTER native bones have been committed. Rendering later matches
			// the exact object/buffer/epoch AND consumed bone, not current_muzzle().
			if (layout.muzzle >= 0)
				weapon_render_pose::publish_solved(
				    {reinterpret_cast<std::uintptr_t>(object),
				     reinterpret_cast<std::uintptr_t>(object->matrices),
				     object->timestamp,
				     static_cast<std::uint16_t>(layout.muzzle),
				     object->matrices[layout.muzzle],
				     muzzle,
				     controller_input::clock::now(),
				     control_grip,
				     static_cast<std::uint16_t>(layout.laser >= 0 ? layout.laser : 256),
				     layout.laser >= 0 ? object->matrices[layout.laser] : bone{}});
			if (independent)
				owned_native::accept(object, object->matrices, object->timestamp);
			published_muzzle = layout.muzzle >= 0;
			if (weapons::carry::active())
			{
				auto wrists = targets;
				for (unsigned h = 0; h < 2; ++h)
					wrists[h].position = add(solved[layout.arms[h].wrist].position, view_offset);
				const std::array arms{capture_arm(layout, solved, 0, view_offset, spatial.units_per_meter),
				                      capture_arm(layout, solved, 1, view_offset, spatial.units_per_meter)};
				weapons::carry::publish_hands(wrists,
				                              arms,
				                              input,
				                              reinterpret_cast<std::uintptr_t>(object),
				                              reinterpret_cast<std::uintptr_t>(object->matrices),
				                              object->timestamp,
				                              view_offset);
			}
			for (int hand = 0; hand < 2; ++hand)
			{
				probe.solved_wrist[hand] = solved[layout.arms[hand].wrist].position;
				probe.solved_shoulder[hand] = solved[layout.arms[hand].shoulder].position;
				probe.solved_elbow[hand] = solved[layout.arms[hand].elbow].position;
			}
			probe.solved = true;
			++applications;
			if (limited[0])
				++limited_left;
			if (limited[1])
				++limited_right;
			reason = "independent hands applied";
		}

		std::string format_status()
		{
			const auto input = controller_input::latest();
			std::ostringstream text;
			{
				const std::unique_lock lock(mutex,std::try_to_lock);
				if (!lock.owns_lock()) return "[VR hands] snapshot=busy; solver evidence unavailable at report time\n";
				text << "[VR hands] installed=" << installed
				     << " enabled=" << (enabled && enabled->current.enabled) << " calls=" << calls
				     << " applied=" << applications << " skipped=" << skipped
				     << " reach_limited=" << limited_left << '/' << limited_right
				     << " transition_poses=" << transition_poses << " state=" << reason << '\n'
				     << "epoch=" << solver().last_timestamp << " input=" << probe.input_sequence
				     << " input_age_ms=" << probe.input_age_ms << " camera_age_ms=" << probe.camera_age_ms
				     << " last_pose_solved=" << probe.solved << '\n';
				for (const auto& context : owned_solvers)
					if (context.last_object)
						text << "solver_object=" << context.last_object
						     << " resource=" << context.last_resource_generation
						     << " epoch=" << context.last_timestamp << " profile="
						     << (context.binding.active_profile ? context.binding.active_profile->id
						                                        : std::string_view{"none"})
						     << " ads_approach_m=" << context.ads_approach_meters
						     << " safe_index_blend=" << context.safe_index_weight
						     << " safe_index_applied=" << context.safe_index_applied << '\n';
				text << "trigger_touch_active=" << input.trigger_touch[0].active << '/'
				     << input.trigger_touch[1].active << " trigger_touch_down=" << input.trigger_touch[0].down
				     << '/' << input.trigger_touch[1].down << " trigger_click=" << input.trigger[0].down
				     << '/' << input.trigger[1].down << " touch_input_sequence=" << input.sequence
				     << " safe_index_blend=" << solver().safe_index_weight
				     << " safe_index_applied=" << solver().safe_index_applied << '\n';
				text << "contract=" << probe.contract << " muzzle_forward_dot=" << probe.muzzle_forward_dot
				     << " holding_hand=" << static_cast<int>(probe.owner.rear)
				     << " weapon_token=" << probe.owner.weapon << " hold_revision=" << probe.owner.revision
				     << " models=" << probe.model_count << " bones=" << probe.bone_count
				     << " hands=" << probe.hands_name.data() << " weapon=" << probe.weapon_name.data()
				     << '\n';
				text << "elbows=body_swing_outward_down shoulders=hmd_yaw_estimate half_width/down/back_m="
				     << probe.offsets.half_width_meters << '/' << probe.offsets.down_meters << '/'
				     << probe.offsets.back_meters << " head=[" << probe.head_position[0] << ','
				     << probe.head_position[1] << ',' << probe.head_position[2] << "]\n";
				text << "wrist_offset=grip_local_point inward/back/up_m="
				     << probe.wrist_offsets.inward_meters << '/' << probe.wrist_offsets.back_meters << '/'
				     << probe.wrist_offsets.up_meters << '\n';
				text << "forearm_twist=final_anatomical_axial applied_hands=" << probe.forearm_twist_hands
				     << " bones=" << solver().binding.twist_binding.arms[0].node << '/'
				     << solver().binding.twist_binding.arms[1].node << '\n';
				text << "equipment_body_estimated=" << probe.storage_body.valid << " position=["
				     << probe.storage_body.position[0] << ',' << probe.storage_body.position[1] << ','
				     << probe.storage_body.position[2] << "] forward=[" << probe.storage_body.yaw_axis[0][0]
				     << ',' << probe.storage_body.yaw_axis[0][1] << ',' << probe.storage_body.yaw_axis[0][2]
				     << "]\n";
				text << "grip_profile="
				     << (solver().binding.active_profile ? solver().binding.active_profile->id : "none")
				     << " ads_approach_m=" << solver().ads_approach_meters << " variant="
				     << (solver().binding.active_profile ? solver().binding.active_profile->variant : "none")
				     << " state=" << probe.grip_reason << " animation_query=" << animation_query_ready
				     << " support=" << static_cast<int>(probe.owner.support)
				     << " distance_m=" << probe.grip.distance_meters << " blend=" << probe.grip.blend
				     << " equip_suppressed=" << probe.equip_suppressed << '\n';
				for (int hand = 0; hand < 2; ++hand)
				{
					text << (hand == 0 ? "left" : "right") << " target/native/solved (view-offset-relative):";
					for (const auto point :
					     {probe.target[hand], probe.native_wrist[hand], probe.solved_wrist[hand]})
						text << " [" << point[0] << ',' << point[1] << ',' << point[2] << ']';
					text << '\n';
					text << (hand == 0 ? "left" : "right") << " shoulder target/native/solved:";
					for (const auto point : {probe.shoulder_target[hand],
					                         probe.native_shoulder[hand],
					                         probe.solved_shoulder[hand]})
						text << " [" << point[0] << ',' << point[1] << ',' << point[2] << ']';
					text << '\n';
					text << (hand == 0 ? "left" : "right") << " elbow native/solved:";
					for (const auto point : {probe.native_elbow[hand], probe.solved_elbow[hand]})
						text << " [" << point[0] << ',' << point[1] << ',' << point[2] << ']';
					text << '\n';
				}
			}
			// No console or disk I/O while holding the pose/native skeleton locks.
			text << weapons::viewmodel_visibility::status();
			text << empty_native::status();
			text << scripted_arms::status();
			return text.str();
		}

		void print_status()
		{
			const auto formatted = format_status();
			console::info("%s", formatted.c_str());
			if (!utils::io::write_file_atomic("minidumps/overlord-hands-latest.txt", formatted))
				console::warn("[VR hands] Could not persist hand status snapshot\n");
		}
	} // namespace

	std::string status()
	{
		return format_status();
	}

	class component final : public component_interface
	{
	  public:
		void post_unpack() override
		{
			animation_query_ready = weapons::initialize_animation_query();
			if (!animation_query_ready)
				console::error("[VR grips] Animation query signature rejected; authored grip presentation "
				               "unavailable\n");
			grips_enabled = dvars::register_bool(
			    "vr_weaponGrips",
			    true,
			    game::DVAR_FLAG_SAVED,
			    "Authored weapon grip profiles, open free hand and equip presentation suppression");
			enabled = dvars::register_bool(
			    "vr_independentHands",
			    true,
			    game::DVAR_FLAG_SAVED,
			    "Independent controller-driven hands, including empty-hand presentation");
			command::add("vr_hands_status", print_status);
			shoulder_half_width =
			    dvars::register_float("vr_shoulderHalfWidth",
			                          0.18f,
			                          0.10f,
			                          0.35f,
			                          game::DVAR_FLAG_SAVED,
			                          "Estimated shoulder distance to each side of HMD in meters");
			shoulder_down = dvars::register_float("vr_shoulderDown",
			                                      0.20f,
			                                      0.05f,
			                                      0.45f,
			                                      game::DVAR_FLAG_SAVED,
			                                      "Estimated shoulder distance below HMD in meters");
			shoulder_back =
			    dvars::register_float("vr_shoulderBack",
			                          0.08f,
			                          -0.10f,
			                          0.30f,
			                          game::DVAR_FLAG_SAVED,
			                          "Estimated shoulder distance behind HMD in meters (negative: forward)");
			constexpr std::uint8_t bytes[]{0x40,
			                               0x55,
			                               0x53,
			                               0x56,
			                               0x57,
			                               0x41,
			                               0x54,
			                               0x41,
			                               0x56,
			                               0x41,
			                               0x57,
			                               0x48,
			                               0x8d,
			                               0xac,
			                               0x24,
			                               0x50,
			                               0xff,
			                               0xff,
			                               0xff};
			std::array<std::uint8_t, sizeof(bytes)> mask{};
			mask.fill(0xff);
			constexpr std::uint8_t lookup_bytes[]{0x48, 0x63, 0xc1, 0x48, 0x8d, 0x0d, 0xa6, 0xc2, 0xb6, 0x0a,
			                                      0x0f, 0xbf, 0x0c, 0x41, 0x85, 0xc9, 0x74, 0x13, 0x48, 0x8d,
			                                      0x04, 0xc9, 0x48, 0xc1, 0xe0, 0x06, 0x48, 0x8d, 0x0d, 0xff,
			                                      0x71, 0x88, 0x0a, 0x48, 0x03, 0xc1, 0xc3, 0x33, 0xc0, 0xc3};
			std::array<std::uint8_t, sizeof(lookup_bytes)> lookup_mask{};
			lookup_mask.fill(0xff);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(calc_skeleton_address),
			                                                 {bytes, mask.data(), sizeof(bytes)}) ||
			    !utils::hook_validation::verify_masked_bytes(
			        reinterpret_cast<void*>(client_object_address),
			        {lookup_bytes, lookup_mask.data(), sizeof(lookup_bytes)}))
			{
				reason = "skeleton hook signature mismatch";
				console::error("[VR hands] %s\n", reason);
				return;
			}
			calc_skeleton_hook.create(calc_skeleton_address, calc_skeleton_stub);
			// Do not acquire the solver lock from the native unload barrier. Each
			// context discards borrowed bindings before its next skeleton epoch.
			fastfiles::on_pre_unload([] { asset_generation.fetch_add(1, std::memory_order_relaxed); });
			scene_models::skeletal_model::enable();
			installed = true;
		}
		void pre_destroy() override
		{
			alive.store(false, std::memory_order_relaxed);
		}
	};
} // namespace vr::gameplay::hands
REGISTER_COMPONENT(vr::gameplay::hands::component)
