#include <std_include.hpp>
#include "../debug_options.hpp"
#include "component/scene_model_record.hpp"
#include "vehicles/runtime.hpp"
#include "carry_interaction.hpp"
#include "grenade_runtime.hpp"
#include "special_equipment_runtime.hpp"
#include "hand_interaction/runtime.hpp"
#include "native_scripted_control.hpp"
#include "notebook_runtime.hpp"
#include "designator_events.hpp"
#include "native_claymore.hpp"
#include "world_interaction.hpp"
#include "interaction_debug.hpp"
#include "weapon_carry_runtime.hpp"
#include "nightvision_runtime.hpp"
#include "weapon_instance_cache.hpp"
#include "drop_presentation.hpp"
#include "empty_hands_native.hpp"
#include "weapon_carry_render_cache.hpp"
#include "weapon_render_pose.hpp"
#include "weapon_render_owner.hpp"
#include <utils/native_memory.hpp>
#include "independent_fire_runtime.hpp"
#include "native_carry.hpp"
#include "native_carry_model.hpp"
#include "weapon_holsters.hpp"
#include "official_cheats.hpp"
#include "holster_presentation.hpp"
#include "hands/attachment_pose.hpp"
#include "weapon_carry_grip.hpp"
#include "grip_edges.hpp"
#include "physical_reload_runtime.hpp"
#include "reload_item_runtime.hpp"
#include "cylinder_runtime.hpp"
#include "tube_runtime.hpp"
#include "break_action_runtime.hpp"
#include "launcher_runtime.hpp"
#include "heartbeat_runtime.hpp"
#include "underbarrel_runtime.hpp"
#include "equipment_runtime.hpp"
#include "weapon_interaction.hpp"
#include "weapon_feedback.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "hands/position_offset.hpp"
#include "shoulder_anchors.hpp"
#include "component/scene_models.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>
#include <mutex>
#include <sstream>

namespace vr::gameplay::weapons::carry
{
	namespace
	{
		std::atomic_bool alive{true}, running{};
		game::dvar_t* enabled{};
		std::array<game::dvar_t*, 6> slot_settings{};
		inventory owned; // Server is the only writer; readers receive a published copy.
		std::mutex publication_mutex;
		inventory published;
		instance_cache<scene, inventory::capacity> scenes;
		instance_cache<support_carry_rotation, inventory::capacity> support_rotations;
		struct model_pose
		{
			game::XModel* model{};
			hands::anchor world{};
			identity held{};
			hands::anchor local{};
			std::size_t slot{};
		};
		struct models_frame
		{
			std::array<model_pose, 160> models{};
			std::size_t count{};
			float units{};
			controller_input::clock::time_point at{};
			holster_layout layout{};
			std::uint64_t reference{};
		};
		models_frame render_models{};
		std::array<storage_scene, 2> render_storage{};
		struct hand_frame
		{
			std::array<hands::anchor, 2> wrists{};
			std::uint64_t sequence{}, reference{};
			controller_input::clock::time_point at{};
			std::array<hands::arm_geometry, 2> arms{};
			std::array<hold, 2> owners{};
		};
		hand_frame render_hands{};
		// Native scene entries retain these addresses through queued preparation.
		// A held part's address must not change when inventory iteration changes.
		std::array<unsigned short, inventory::capacity * 32> held_lighting{};
		render_cache render_poses;
		std::atomic_uint64_t render_matches{};
		std::array<std::atomic_uint64_t, 4> render_misses{}; // record, epoch/part, ownership, origin
		hold presented{};
		grip_edge_gate edges;
		scripted_control::selection_pause script_pause;
		identity preferred{};
		std::uint32_t expected_selection{}, last_selection{};
		bool selection_pending{};
		const void* player{};
		int game_time{};
		std::uint64_t timeline{};
		struct abdominal_request
		{
			std::uint32_t weapon{};
			hand actor{hand::none};
			std::uint64_t reference{}, releases{}, generation{};
			controller_input::clock::time_point at{};
			bool cancelled{}, resumed{}, restarted{}, delay_logged{};
			int selected_ms{-1}, unlocked_ms{-1};
		};
		abdominal_request pending_abdominal, public_abdominal;
		controller_input::clock::time_point selection_at{}, last_pose_at{};
		std::uint64_t last_sequence{}, reference{}, transitions{}, rejections{};
		struct release_record
		{
			std::uint32_t weapon{};
			int hand{};
			unsigned mask{};
			outcome action{};
			double input_ms{}, work_ms{};
			std::string detail;
		};
		std::array<release_record, 16> release_history{};
		std::size_t release_cursor{};
		struct edge_record
		{
			std::uint64_t sequence{};
			unsigned poses{}, down{};
			grip_edges event;
			std::array<controller_input::digital_action, 2> buttons;
		};
		std::array<edge_record, 32> edge_history{};
		std::size_t edge_cursor{};
		unsigned deferred_mask{};
		std::array<hands::vec, 2> last_positions{};
		const char* reason{"waiting for VR gameplay"};
		interaction_context batch;
		bool batch_ready{};
		void clear()
		{
			hand_interaction::suspend();
			equipment::suspend(true);
			script_pause.reset();
			heartbeat::suspend();
			underbarrel::suspend();
			interaction::suspend();
			owned.clear();
			edges = {};
			preferred = {};
			last_sequence = 0;
			selection_pending = false;
			pending_abdominal = {};
			const std::lock_guard lock(publication_mutex);
			scenes = {};
			support_rotations = {};
			render_models = {};
			render_storage = {};
			render_hands = {};
		}
		scene pose(const instance&,
		           const std::array<hands::anchor, 2>&,
		           const controller_input::frame&,
		           unsigned valid_hands = 3);
		template <class T> bool read(const void* source, std::size_t offset, T& value) noexcept
		{
			return source && utils::native_memory::read_bytes(
			                     &value, static_cast<const std::byte*>(source) + offset, sizeof(value));
		}
		scene_models::placement_result prepare_held(const scene_models::preparation& preparing,
		                                            const void* entry,
		                                            game::GfxPlacement& placed,
		                                            game::GfxPlacement& previous) noexcept
		{
			using result = scene_models::placement_result;
			std::uintptr_t handle{};
			if (!scene_models::native_entry::lighting(entry, handle))
				return result::unchanged;
			const auto begin = reinterpret_cast<std::uintptr_t>(held_lighting.data());
			if (handle < begin || handle >= begin + sizeof(held_lighting) ||
			    (handle - begin) % sizeof(unsigned short))
				return result::unchanged;
			const auto reject = [](std::size_t why)
			{
				++render_misses[why];
				return result::omit;
			};
			if (!active() || !scripted_control::predicted_allowed())
				return reject(2);
			std::uintptr_t model{};
			std::array<float, 12> camera{};
			weapon_render_pose::snapshot skinned;
			if (!scene_models::native_entry::model(entry, model) ||
			    !read(preparing.record, engine_stereo_view::h2_view_origin_offset, camera) ||
			    !weapon_render_pose::for_record(
			        reinterpret_cast<std::uintptr_t>(preparing.record), camera, skinned))
				return reject(0);
			render_part part;
			render_sample sample;
			{
				const std::lock_guard lock(publication_mutex);
				if (!render_poses.find(skinned.object,
				                       skinned.matrices,
				                       skinned.epoch,
				                       (handle - begin) / sizeof(unsigned short),
				                       model,
				                       part,
				                       sample))
					return reject(1);
				const auto* live = published.find(part.id);
				if (!live || live->at != location::held || !same_render_carrier(live->owner, part.owner) ||
				    presented.id() == part.id || sample.reference != skinned.muzzle.reference_generation)
					return reject(2);
			}
			const auto now = controller_input::clock::now();
			if (now < sample.at || now - sample.at > 150ms)
				return reject(2);
			hands::vec origin{};
			if (!read(
			        preparing.record, engine_stereo_view::h2_current_model_placement_origin_offset, origin) ||
			    !std::all_of(origin.begin(), origin.end(), [](float x) { return std::isfinite(x); }))
				return reject(3);
			const auto world = hands::add(part.relative.position, origin);
			std::copy(world.begin(), world.end(), placed.origin);
			std::copy(part.relative.rotation.begin(), part.relative.rotation.end(), placed.quat);
			previous = placed;
			++render_matches;
			return result::replace;
		}
		void submit_models()
		{
			if (!running.load() || !alive.load() || !scripted_control::predicted_allowed())
				return;
			models_frame frame;
			{
				const std::lock_guard lock(publication_mutex);
				frame = render_models;
			}
			const auto now = controller_input::clock::now();
			if (now < frame.at || now - frame.at > 150ms)
				return;
			inventory state;
			{
				const std::lock_guard lock(publication_mutex);
				state = published;
			}
			for (std::size_t i = 0; i < frame.count; ++i)
			{
				const auto& v = frame.models[i];
				if (!v.model)
					continue;
				auto world = v.world;
				if (v.held)
				{
					if (hands::owned_native::active())
						continue;
					const auto* owner = state.find(v.held);
					if (!owner || owner->at != location::held || !valid_hand(owner->owner.holding_hand()))
						continue;
					// Coarse placement admits geometry only. Final placement is bound
					// to this native record's skinned arms in prepare_held.
					if (v.slot >= held_lighting.size())
						continue;
				}
				game::GfxScaledPlacement placement{};
				placement.scale = 1;
				std::copy(world.position.begin(), world.position.end(), placement.base.origin);
				std::copy(world.rotation.begin(), world.rotation.end(), placement.base.quat);
				// Legacy held fallback only; waist weapons own native skeletons.
				float color[4]{1, 1, 1, 1};
				scene_models::submit(
				    v.model, &placement, 1u, &held_lighting[v.slot], color, color, color, .25f * frame.units);
			}
		}
		scene pose(const instance& v,
		           const std::array<hands::anchor, 2>& wrists,
		           const controller_input::frame& input,
		           unsigned valid_hands)
		{
			scene out;
			{
				const std::lock_guard lock(publication_mutex);
				if (const auto* cached = scenes.find(v.id))
					out = *cached;
			}
			if (!out.owner.weapon)
				return {};
			// Cache local contacts only. Reusing a recent world pose freezes a
			// newly secondary gun for 50 ms and then jumps to another pose path.
			// Contacts are rigid gun-local data. Tracking/reference changes invalidate
			// old world positions, but can safely reuse these local contacts.
			const auto driver = valid_hand(v.owner.rear) ? v.owner.rear : v.owner.support;
			if (!valid_hand(driver) || !(valid_hands & (1u << static_cast<unsigned>(driver))))
				return {};
			const auto h = static_cast<int>(driver);
			const auto local = valid_hand(v.owner.rear) ? out.wrists[h] : out.supports[h];
			auto rotation = wrists[h].rotation;
			rotation = hands::normalize(hands::multiply(
			    rotation,
			    support_basis(v.owner, input.reference_generation).value_or(out.control_rotations[h])));
			auto position = wrists[h].position;
			if (v.owner.can_fire() && out.authored && out.authored->forearm)
			{
				head_pose_bridge::spatial_frame body;
				std::array<hands::vec, 2> shoulders;
				const auto dimensions = out.shoulder_dimensions;
				if (!head_pose_bridge::get_spatial_frame(body) ||
				    !hands::make_shoulders(
				        body, {}, {dimensions[0], dimensions[1], dimensions[2]}, shoulders))
					return {};
				const auto mounted = hands::mount_forearm(*out.authored->forearm,
				                                          out.arm_lengths[h][0],
				                                          out.arm_lengths[h][1],
				                                          {position, rotation},
				                                          shoulders[h],
				                                          body.head_yaw_axis,
				                                          h);
				if (!mounted.limb.valid)
					return {};
				rotation = mounted.rotation;
				position = mounted.limb.wrist;
			}
			const auto support_plan = hand_interaction::pose(v.owner.support);
			if (valid_hands == 3 && valid_hand(v.owner.rear) && valid_hand(v.owner.support) &&
			    !v.policy.promote_support &&
			    (!support_plan.driver ||
			     hand_interaction::has(support_plan.abilities, hand_interaction::capability::aim)))
				rotation = aimed_rotation(aim_rule::two_hand,
				                          rotation,
				                          wrists[h].position,
				                          wrists[1 - h].position,
				                          hands::sub(out.supports[1 - h].position, local.position));
			out.gun = {hands::sub(position, hands::rotate(rotation, local.position)), rotation};
			out.support_available = true;
			out.has_moving_control = false;
			if (out.authored && out.authored->tube && out.authored->tube->lever)
			{
				const auto action = tube::current(v.owner.id());
				const auto motion = tube::held_lever_pose(action, v.owner);
				out.support_available = !action.active || !lever::blocks_support(motion);
				if (action.active)
				{
					if (v.owner.can_fire())
						out.gun = lever::apply_gun_pose(
						    out.gun,
						    lever::gun_offset(*out.authored->tube->lever, motion, v.owner.rear),
						    out.wrists[h],
						    valid_hand(v.owner.support) ? &out.supports[1 - h] : nullptr);
					out.has_moving_control =
					    motion.open >= out.authored->tube->interaction.lever.grip_separation &&
					    !motion.spinning;
					auto contact = motion;
					contact.grasped = true;
					for (int side = 0; side < 2; ++side)
						out.moving_controls[side] =
						    hands::pose_math::compose(hands::pose_math::inverse(lever::gun_offset(
						                                  *out.authored->tube->lever, contact, hand(side))),
						                              out.wrists[side]);
				}
			}
			out.owner = v.owner;
			out.sequence = input.sequence;
			out.reference = input.reference_generation;
			out.at = input.sampled_at;
			out.support_candidate = hand::none;
			return out;
		}
		hold projection() noexcept
		{
			// Script devices need native selection for their mission watcher and
			// trigger events. A newly picked-up bullet gun uses independent fire.
			for (const auto& v : owned.instances())
				if (v.at == location::held && v.policy.abdominal && v.owner.can_fire() &&
				    controller_fire_delivery(v.id.weapon) == fire_delivery::scripted_device)
				{
					preferred = v.id;
					return v.owner;
				}
			if (const auto* v = owned.find(preferred); v && v->at == location::held)
				return v->owner;
			for (const auto& v : owned.instances())
				if (v.at == location::held)
				{
					preferred = v.id;
					return v.owner;
				}
			preferred = {};
			return {};
		}
		void publish()
		{
			const auto owner = projection();
			if (owner.id())
				native_ammunition::project(owner.id());
			const std::lock_guard lock(publication_mutex);
			published = owned;
			presented = owner;
			public_abdominal = pending_abdominal;
			scenes.retain([](identity id) { return published.find(id) != nullptr; });
		}
		bool request_selection(std::uint32_t token)
		{
			if (equipment::special::notebook::controlling())
				return false;
			// Releasing the other held gun must not interrupt the mission's draw
			// before its cancellation watcher has been installed.
			if (pending_abdominal.weapon && token != pending_abdominal.weapon)
				return false;
			if (native_carry::select(token))
			{
				expected_selection = token;
				selection_pending = true;
				selection_at = controller_input::clock::now();
				return true;
			}
			return false;
		}
		void complete(const result& r, hand h)
		{
			if (r.action == outcome::unchanged)
				return;
			if (r.action == outcome::rejected)
			{
				++rejections;
				reason = "release rejected; regrip and release to retry";
				return;
			}
			++transitions;
			if (r.action == outcome::drawn || r.action == outcome::stowed || r.action == outcome::exchanged)
				feedback::carry_confirmation(h, controller_input::latest());
			if (r.action == outcome::drawn)
				preferred = r.subject;
			if (r.action == outcome::exchanged)
			{
				preferred = r.received;
				edges.latch(h);
			}
			if (r.action == outcome::promoted)
				edges.latch(h);
			const auto view = projection();
			request_selection(view.weapon);
			publish();
			invalidate_muzzle();
			reason = r.action == outcome::stowed       ? "weapon stowed; hand empty"
			         : r.action == outcome::exchanged  ? "holster exchange committed; regrip required"
			         : r.action == outcome::carry_only ? "foregrip carry only; reacquire control grip to fire"
			         : r.action == outcome::promoted   ? "remaining hand owns control; fresh trigger required"
			         : r.action == outcome::dropped    ? "native physical weapon dropped"
			                                           : "carry interaction committed";
		}
		void tick()
		{
			batch_ready = false;
			if (!alive.load())
				return;
			if (cheats::update())
			{
				publish();
				invalidate_muzzle();
				return;
			}
			const auto head = head_pose_bridge::get_status();
			const bool requested = enabled && enabled->current.enabled && head.enabled;
			const auto native = native_carry::observe();
			if (!requested || !native.valid)
			{
				if (running.exchange(false) || player)
				{
					clear();
					player = nullptr;
					publish();
					invalidate_muzzle();
				}
				return;
			}
			const auto now = controller_input::clock::now();
			if (player != native.player || native.time < game_time || timeline != native.timeline)
			{
				clear();
				owned.reconcile_instances({native.owned.data(), native.count});
				const auto selected = native_ammunition::projected_identity(native.selected);
				if (const auto* item = owned.find(selected);
				    item && !item->policy.abdominal && owned.equip(selected, hand::right))
					preferred = selected;
				player = native.player;
				last_selection = native.selected;
				running = true;
				reason = "native owned inventory admitted";
			}
			game_time = native.time;
			timeline = native.timeline;
			if (!owned.reconcile_instances({native.owned.data(), native.count}))
			{
				hand_interaction::suspend();
				reason = "invalid native instance snapshot rejected";
				return;
			}
			if (pending_abdominal.weapon)
			{
				const auto input = controller_input::latest();
				const auto h = unsigned(pending_abdominal.actor);
				pending_abdominal.cancelled =
				    pending_abdominal.cancelled || !input.focused ||
				    input.reference_generation != pending_abdominal.reference || !input.squeeze[h].active ||
				    !input.squeeze[h].down || input.squeeze[h].releases != pending_abdominal.releases ||
				    input.squeeze[h].generation != pending_abdominal.generation ||
				    owned.in_hand(pending_abdominal.actor) || !scripted_control::allowed(native.player);
				const auto* item = owned.find_definition(pending_abdominal.weapon);
				const auto activation =
				    equipment::special::designator_events::activation(pending_abdominal.weapon);
				unsigned actual_weapon{};
				std::memcpy(&actual_weapon, static_cast<const std::byte*>(native.player) + 0x3bc, 4);
				const bool diagnose = debug_options::enabled(debug_options::probe::weapon_events);
				const auto elapsed = diagnose ? int(std::chrono::duration_cast<std::chrono::milliseconds>(
				                                        now - pending_abdominal.at)
				                                        .count())
				                              : 0;
				if (diagnose && (actual_weapon & 511) == pending_abdominal.weapon &&
				    pending_abdominal.selected_ms < 0)
					pending_abdominal.selected_ms = elapsed;
				if (diagnose && activation.active && !activation.locked && pending_abdominal.unlocked_ms < 0)
					pending_abdominal.unlocked_ms = elapsed;
				if (diagnose && activation.device && elapsed >= 100 && !pending_abdominal.delay_logged)
				{
					pending_abdominal.delay_logged = true;
					console::info(
					    "[VR abdominal draw] pending ms=%d phase=%d active=%d enabled=%d locked=%d actual=%u desired=%u cancelled=%d\n",
					    elapsed,
					    int(activation.phase),
					    activation.active,
					    activation.enabled,
					    activation.locked,
					    actual_weapon,
					    native.selected,
					    pending_abdominal.cancelled);
				}
				const bool ready = scripted_control::allowed(native.player) &&
				                   (actual_weapon & 511) == pending_abdominal.weapon &&
				                   (!activation.device || activation.ready());
				if (item && scripted_control::allowed(native.player) && activation.device &&
				    activation.enabled && activation.active && activation.locked && !activation.selected &&
				    activation.phase == equipment::special::designator_events::activation_phase::opening &&
				    !pending_abdominal.resumed && native.selected != pending_abdominal.weapon)
					pending_abdominal.resumed = native_carry::select(pending_abdominal.weapon);
				// The native waiter, not an arbitrary retry timer, admits a redraw.
				if (activation.device && activation.enabled && !activation.active &&
				    !pending_abdominal.cancelled && !pending_abdominal.restarted &&
				    activation.phase == equipment::special::designator_events::activation_phase::idle)
				{
					pending_abdominal.restarted =
					    equipment::special::designator_events::request_activation(pending_abdominal.weapon);
				}
				if (native.selected == pending_abdominal.weapon && item && ready)
				{
					const auto request = pending_abdominal;
					pending_abdominal = {};
					if (diagnose && activation.device && request.delay_logged)
						console::info(
						    "[VR abdominal draw] handoff ms=%d selected_ms=%d unlocked_ms=%d cancelled=%d\n",
						    elapsed,
						    request.selected_ms,
						    request.unlocked_ms,
						    request.cancelled);
					if (!request.cancelled && owned.equip(item->id, request.actor))
					{
						preferred = item->id;
						last_selection = native.selected;
						selection_pending = false;
						edges.adopt(request.actor, input, now);
					}
					else
					{
						request_selection(projection().weapon);
						last_selection = native.selected;
					}
				}
				else if (now - pending_abdominal.at > 2500ms &&
				         !(item && activation.device && activation.enabled && activation.active &&
				           activation.locked))
					pending_abdominal = {};
			}
			if (!scripted_control::allowed(native.player) || equipment::special::notebook::native_control())
			{
				const bool entering = !script_pause.suspended();
				const bool dead = player_life::dead(native.player);
				if (dead)
				{
					// Release VR grip ownership only: native death/checkpoint code
					// owns the world weapon and inventory. Do not issue a live drop.
					owned.put_away();
					preferred = {};
					pending_abdominal = {};
				}
				script_pause.suspend(last_selection);
				selection_pending = false;
				const auto input = controller_input::latest();
				edges.consume(input, false, now);
				last_sequence = input.sequence;
				const bool world_use = !dead && sequences::for_player(native.player).allow_world_use &&
				                       !equipment::special::notebook::controlling();
				hand_interaction::suspend(world_use || (!dead && vehicles::active()));
				if (!world_use)
					interaction::suspend();
				heartbeat::suspend();
				underbarrel::suspend();
				equipment::suspend();
				invalidate_muzzle();
				independent_fire::command(false);
				independent_fire::update();
				// Keep committed inventory/grips; discard world-space leases.
				if (entering || dead)
				{
					const std::lock_guard lock(publication_mutex);
					scenes = {};
					support_rotations = {};
					render_models = {};
					render_storage = {};
					render_hands = {};
				}
				publish();
				reason =
				    dead ? "player dead; both grips released" : "native script owns weapons; carry retained";
				return;
			}
			if (script_pause.suspended())
			{
				if (script_pause.resume(native.selected))
					last_selection = native.selected;
				reason = "native weapons restored; reconciling final script selection";
			}
			// A native requested selection is not an acknowledgement. In particular,
			// the mission draw owns its switch lock until the startup watcher exists.
			// Do not auto-equip the requested weapon to the right hand underneath it.
			const bool mission_transition = pending_abdominal.weapon ||
			                                equipment::special::designator_events::owns_native_transition() ||
			                                equipment::special::notebook::controlling();
			if (!mission_transition && selection_pending)
			{
				if (native.selected == expected_selection)
				{
					selection_pending = false;
					last_selection = native.selected;
				}
				else if (native.selected != last_selection)
				{
					selection_pending = false;
					if (const auto id = native_ammunition::projected_identity(native.selected);
					    owned.find(id) && !owned.find(id)->policy.abdominal && owned.equip(id, hand::right))
					{
						preferred = id;
						edges = {};
					}
					else if (!native.selected)
					{
						owned.put_away();
						preferred = {};
						edges = {};
					}
					last_selection = native.selected;
					reason = "external equipment selection superseded pending carry selection";
				}
				else if (now - selection_at > 1500ms)
				{
					request_selection(projection().weapon);
					reason = "retrying native selection acknowledgement";
				}
			}
			else if (!mission_transition && native.selected != last_selection)
			{
				// Script/keyboard selection is an explicit equipment request. Console
				// give does not request selection and never seizes a holding hand.
				if (const auto id = native_ammunition::projected_identity(native.selected);
				    owned.find(id) && !owned.find(id)->policy.abdominal && owned.equip(id, hand::right))
				{
					preferred = id;
					edges = {};
				}
				else if (!native.selected)
				{
					owned.put_away();
					preferred = {};
					edges = {};
				}
				last_selection = native.selected;
			}
			publish();
			const auto input = controller_input::latest();
			head_pose_bridge::spatial_frame body;
			const auto* paused = game::Dvar_FindVar("cl_paused");
			const bool gameplay = input.focused && !input.orientation_settling && head.pose_available &&
			                      !head.recenter_pending && *game::keyCatchers == 0 && paused &&
			                      !paused->current.integer && head_pose_bridge::get_spatial_frame(body) &&
			                      body.generation == input.reference_generation && now >= body.captured_at &&
			                      now - body.captured_at <= 150ms;
			if (!gameplay || now < input.sampled_at || now - input.sampled_at > 150ms)
			{
				hand_interaction::suspend();
				edges.consume(input, false, now);
				interaction::suspend();
				heartbeat::suspend();
				underbarrel::suspend();
				equipment::suspend();
				return;
			}
			if (input.sequence == last_sequence)
				return;
			std::array<head_pose_bridge::world_pose, 2> hands;
			std::array<hands::anchor, 2> wrists{};
			const auto* inward = game::Dvar_FindVar("vr_handOffsetInward");
			const auto* back = game::Dvar_FindVar("vr_handOffsetBack");
			const auto* up = game::Dvar_FindVar("vr_handOffsetUp");
			if (!inward || !back || !up)
			{
				hand_interaction::suspend();
				return;
			}
			auto interaction_input = input;
			unsigned valid_hands{};
			for (int h = 0; h < 2; ++h)
			{
				head_pose_bridge::world_pose aim;
				if (!input.grip[h].valid ||
				    !head_pose_bridge::tracking_to_world(body, input.grip[h].tracking, hands[h]) ||
				    !input.aim[h].valid ||
				    !head_pose_bridge::tracking_to_world(body, input.aim[h].tracking, aim) ||
				    !hands::tracked_wrist(input,
				                          body,
				                          {},
				                          h,
				                          {inward->current.value, back->current.value, up->current.value},
				                          wrists[h]))
				{
					interaction_input.grip[h].valid = false;
					interaction_input.aim[h].valid = false;
					continue;
				}
				valid_hands |= 1u << h;
			}
			// Consume only after per-hand pose admission. A missing opposite hand
			// must neither swallow this hand's release nor replay it on recovery.
			const auto event = edges.consume(interaction_input, gameplay, now);
			if (event.pressed || event.released || event.resumed || event.deferred != deferred_mask)
			{
				const unsigned down = (input.squeeze[0].down ? 1u : 0u) | (input.squeeze[1].down ? 2u : 0u);
				edge_history[edge_cursor++ % edge_history.size()] = {
				    input.sequence, valid_hands, down, event, input.squeeze};
			}
			deferred_mask = event.deferred;
			if (!valid_hands)
			{
				hand_interaction::suspend();
				interaction::suspend();
				underbarrel::suspend();
				equipment::suspend();
				return;
			}
			const auto previous_grips = owned.instances();
			holster_layout layout{slot_settings[0]->current.value,
			                      slot_settings[1]->current.value,
			                      slot_settings[2]->current.value,
			                      slot_settings[3]->current.value,
			                      slot_settings[4]->current.value,
			                      slot_settings[5]->current.value};
			const auto slots = locate_holsters(body, layout);
			if (interaction::debug::body_enabled())
			{
				interaction::debug::body_sample diagnostic;
				diagnostic.slots = slots;
				diagnostic.units = body.units_per_meter;
				diagnostic.reference = body.generation;
				diagnostic.at = input.sampled_at;
				for (unsigned h = 0; h < 2; ++h)
					diagnostic.hands[h] = hands[h].position;
				constexpr std::array places{location::left_waist, location::right_waist, location::back};
				for (unsigned n = 0; n < 3; ++n)
					if (const auto* item = owned.in_slot(places[n]))
						diagnostic.weapons[n] = item->id.weapon;
				interaction::debug::publish_body(diagnostic);
			}
			std::array<hands::vec, 2> velocity{};
			const auto dt = std::chrono::duration<float>(input.sampled_at - last_pose_at).count();
			if (input.reference_generation == reference && dt > .001f && dt < .15f)
				for (std::size_t h = 0; h < 2; ++h)
				{
					if (!(valid_hands & (1u << h)))
						continue;
					velocity[h] = hands::scale(hands::sub(hands[h].position, last_positions[h]), 1 / dt);
					const auto speed = hands::length(velocity[h]), limit = 8 * body.units_per_meter;
					if (speed > limit)
						velocity[h] = hands::scale(velocity[h], limit / speed);
				}
			last_pose_at = input.sampled_at;
			reference = input.reference_generation;
			for (std::size_t h = 0; h < 2; ++h)
				last_positions[h] = hands[h].position;
			last_sequence = input.sequence;
			// Freeze both hand identities so one same-frame release cannot release a
			// newly exchanged gun or transiently promote a hand which also released.
			std::array<identity, 2> release_ids{};
			const auto neutral = neutral_grips(input, now);
			for (int h = 0; h < 2; ++h)
				if (const auto* v = owned.in_hand(static_cast<hand>(h));
				    v && ((event.released & (1u << h)) || (v->policy.abdominal && (neutral & (1u << h)))))
					release_ids[h] = v->id;
			for (int h = 0; h < 2; ++h)
			{
				const auto id = release_ids[h];
				if (!id || (h && release_ids[0] == id))
					continue;
				const auto* v = owned.find(id);
				if (!v)
					continue;
				const auto release_start = controller_input::clock::now();
				const auto record_release = [&](outcome action, const char* detail)
				{
					release_history[release_cursor++ % release_history.size()] = {
					    id.weapon,
					    h,
					    event.released | (v->policy.abdominal ? neutral : 0),
					    action,
					    std::chrono::duration<double, std::milli>(release_start - input.sampled_at).count(),
					    std::chrono::duration<double, std::milli>(controller_input::clock::now() -
					                                              release_start)
					        .count(),
					    detail};
				};
				const auto last = valid_hand(v->owner.rear) ? v->owner.rear : v->owner.support;
				const auto target = (valid_hands & (1u << static_cast<unsigned>(last)))
				                        ? hit(slots, hands[static_cast<int>(last)].position)
				                        : location::absent;
				const auto released_hand = [&](hand actor)
				{ return !valid_hand(actor) || (event.released & (1u << unsigned(actor))); };
				if (sequences::for_player(native.player).block_carry)
				{
					record_release(outcome::retained, "story owns storage and drop policy");
					continue;
				}
				if (v->policy.abdominal)
				{
					const auto r = owned.release(id,
					                             event.released | neutral,
					                             location::absent,
					                             false,
					                             [](const auto&) { return false; });
					record_release(r.action, "mission equipment returns to abdominal slot");
					complete(r, last);
					continue;
				}
				if (target == location::absent && released_hand(v->owner.rear) &&
				    released_hand(v->owner.support) && sequences::for_player(native.player).retain_weapon)
				{
					const auto r = owned.release(
					    id, event.released, target, false, [](const auto&) { return false; }, false);
					record_release(r.action, "scripted combat retains last holding hand");
					publish();
					continue;
				}
				// Interrupt a manipulation while this native instance is still owned;
				// waiting for the asynchronous native selection would permit another
				// reload tick after the gun had already been placed on the body.
				if (valid_hand(v->owner.holding_hand()) &&
				    (event.released & (1u << static_cast<unsigned>(v->owner.holding_hand()))))
				{
					physical_reload::presentation magazine;
					cylinder::presentation revolver;
					break_action::presentation hinged;
					tube::presentation shotgun;
					if (!launcher::prepare_transfer(id) || !underbarrel::prepare_transfer(id) ||
					    !physical_reload::prepare_transfer(id, magazine) ||
					    !cylinder::prepare_transfer(id, revolver) || !tube::prepare_transfer(id, shotgun) ||
					    !break_action::prepare_transfer(id, hinged))
					{
						++rejections;
						reason = "mechanical release preparation rejected; grip retained";
						record_release(outcome::rejected, reason);
						continue;
					}
				}
				const auto before_release = v->owner;
				const auto dropped_pose = pose(*v, wrists, input, valid_hands);
				const auto model = native_carry::geometry(id.weapon);
				const bool blade = dropped_pose.authored && dropped_pose.authored->melee;
				const bool geometry = dropped_pose.owner.id() == id && model.valid &&
				                      (blade || (dropped_pose.has_muzzle && model.has_muzzle));
				const auto world =
				    blade ? dropped_pose.gun
				          : hands::pose_math::compose(
				                dropped_pose.gun,
				                hands::pose_math::compose(dropped_pose.muzzle,
				                                          hands::pose_math::inverse(model.muzzle)));
				const bool clear = geometry && native_carry::clearance(id.weapon, world, body.head_position);
				const std::string clearance_detail =
				    geometry ? native_carry::status() : "held pose or muzzle geometry unavailable";
				const auto r = owned.release(
				    id,
				    event.released,
				    target,
				    clear,
				    [&](const auto& gun)
				    {
					    native_carry::world_key entity;
					    return native_carry::drop(gun, world, velocity[static_cast<int>(last)], entity);
				    },
				    true,
				    !cheats::green_beret());
				if (r.action == outcome::carry_only && dropped_pose.owner.id() == id)
				{
					const auto* retained = owned.find(id);
					if (retained && valid_hand(retained->owner.support))
					{
						const std::lock_guard lock(publication_mutex);
						support_rotations.retain([](identity key) { return owned.find(key) != nullptr; });
						const auto controller = wrists[int(retained->owner.support)].rotation;
						auto orientation = dropped_pose.gun.rotation;
						if (const auto* visible = scenes.find(id);
						    visible && visible->has_support_controller &&
						    visible->owner.revision == before_release.revision &&
						    visible->reference == input.reference_generation &&
						    input.sampled_at >= visible->at && input.sampled_at - visible->at <= 150ms)
							orientation = hands::normalize(
							    hands::multiply(controller,
							                    hands::multiply(hands::conjugate(visible->support_controller),
							                                    visible->gun.rotation)));
						if (auto* rotation = support_rotations.acquire(id))
							rotation->capture(before_release,
							                  retained->owner,
							                  input.reference_generation,
							                  controller,
							                  orientation);
					}
				}
				// Snapshot before selection/presentation work can overwrite the
				// native diagnostic. Slot refusal is not a native collision failure.
				const std::string detail =
				    r.action == outcome::rejected
				        ? (!clear                       ? clearance_detail
				           : target != location::absent ? "weapon incompatible with requested holster"
				                                        : native_carry::status())
				    : r.action == outcome::retained && cheats::green_beret()
				        ? "Green Beret forbids weapon storage"
				    : r.action == outcome::support_released ? "support grip released"
				    : r.action == outcome::promoted || r.action == outcome::carry_only
				        ? "remaining hand retained weapon"
				        : native_carry::status();
				record_release(r.action, detail.c_str());
				complete(r, last);
			}
			bool ordinary_released{};
			for (const auto& v : owned.instances())
				if (v.at == location::held && v.owner.can_fire() && valid_hand(v.owner.support))
				{
					if (underbarrel::current(v.id).owns_support)
						continue;
					const auto held_pose = pose(v, wrists, input, valid_hands);
					const int off = int(v.owner.support);
					if (!underbarrel::ordinary_support_allowed(held_pose, wrists[off], v.owner.support, true))
					{
						const auto result = owned.release(
						    v.id, 1u << off, location::absent, false, [](const instance&) { return false; });
						ordinary_released = result.action == outcome::support_released || ordinary_released;
					}
				}
			if (ordinary_released)
			{
				publish();
				invalidate_muzzle();
			}
			std::array<scene, inventory::capacity> attachment_scenes{};
			for (size_t i = 0; i < owned.instances().size(); ++i)
				if (owned.instances()[i].at == location::held)
					attachment_scenes[i] = pose(owned.instances()[i], wrists, input, valid_hands);
			namespace hi = hand_interaction;
			hi::frame frame;
			frame.input = interaction_input;
			frame.body = body;
			frame.wrists = wrists;
			frame.valid_hands = valid_hands;
			frame.objects = attachment_scenes;
			frame.holsters = slots;
			batch.frame = frame;
			batch.raw_input = input;
			batch.previous = previous_grips;
			batch.released = release_ids;
			batch.edges = event;
			batch.layout = layout;
			batch.player = native.player;
			for (unsigned h = 0; h < 2; ++h)
				batch.hand_positions[h] = hands[h].position;
			batch_ready = true;
		}
	}
	bool active() noexcept
	{
		return alive.load() && running.load();
	}
	inventory capture_inventory(std::span<const owned_instance> entries, std::uint32_t selected)
	{
		auto result = owned;
		const auto native = native_carry::observe();
		const bool fresh = player != native.player || timeline != native.timeline || native.time < game_time;
		if (fresh)
			result.clear();
		if (!result.reconcile_instances(entries))
			return {};
		if (fresh)
			if (const auto* v = result.find(native_ammunition::projected_identity(selected));
			    v && !v->policy.abdominal)
				result.equip(v->id, hand::right);
		return result;
	}
	bool restore_inventory(std::span<const instance> entries, identity selected)
	{
		if (!scheduler::is_executing(scheduler::pipeline::server))
			return false;
		auto next = owned;
		if (!next.restore(entries))
			return false;
		const auto native = native_carry::observe();
		if (!native.valid)
			return false;
		for (const auto& v : entries)
			if (!native_ammunition::instances().find(v.id))
				return false;
		clear();
		owned = next;
		preferred = selected;
		player = native.player;
		game_time = native.time;
		timeline = native.timeline;
		running = true;
		last_selection = native.selected;
		request_selection(selected.weapon);
		publish();
		invalidate_muzzle();
		return true;
	}
	interaction_context* prepare_interactions()
	{
		if (!scheduler::is_executing(scheduler::pipeline::server))
			return nullptr;
		tick();
		return batch_ready ? &batch : nullptr;
	}
	void collect_interactions()
	{
		if (!batch_ready || !scheduler::is_executing(scheduler::pipeline::server))
			return;
		namespace hi = hand_interaction;
		const auto& body = batch.frame.body;
		const auto& wrists = batch.frame.wrists;
		const auto valid_hands = batch.frame.valid_hands;
		const auto& slots = batch.frame.holsters;
		const auto& attachment_scenes = batch.frame.objects;
		for (int h = 0; h < 2; ++h)
		{
			const auto actor = hand(h);
			const auto edge = hi::input(actor, hi::button::grip);
			if (!edge.press || !edge.down || !(valid_hands & (1u << h)) || owned.in_hand(actor))
				continue;
			for (size_t i = 0; i < owned.instances().size(); ++i)
			{
				const auto& v = owned.instances()[i];
				if (v.at != location::held)
					continue;
				const auto& geometry = attachment_scenes[i];
				if (v.owner.can_fire() && v.owner.rear != actor && geometry.authored &&
				    geometry.support_available &&
				    underbarrel::ordinary_support_allowed(geometry, wrists[h], actor) &&
				    support_contact(*geometry.authored,
				                    geometry.gun,
				                    geometry.supports[h],
				                    wrists[int(v.owner.rear)].position,
				                    wrists[h].position,
				                    body.units_per_meter))
				{
					const auto point = hands::pose_math::compose(geometry.gun, geometry.supports[h]).position;
					hi::offer({actor,
					           {hi::object(hi::domain::carry, v.id, 1),
					            hi::role::support,
					            hi::button::grip,
					            hi::recipe::single,
					            hi::capability::aim},
					           edge.event,
					           20,
					           hands::length(hands::sub(point, wrists[h].position)) /
					               (body.units_per_meter * geometry.authored->acquire_meters),
					           1,
					           true,
					           true});
				}
				if (v.owner.rear == hand::none && (!v.policy.right_control || actor == hand::right))
				{
					const auto contact = choose_control_contact(geometry.gun,
					                                            geometry.wrists[h],
					                                            geometry.moving_controls[h],
					                                            geometry.has_moving_control,
					                                            wrists[h].position,
					                                            body.units_per_meter);
					if (contact.valid)
						hi::offer({actor,
						           {hi::object(hi::domain::carry,
						                       v.id,
						                       contact.attachment == control_attachment::moving ? 2 : 0),
						            hi::role::control,
						            hi::button::grip,
						            hi::recipe::single,
						            hi::capability::fire},
						           edge.event,
						           20,
						           contact.distance,
						           1,
						           true,
						           true});
				}
			}
			const auto place = sequences::for_player(batch.player).block_carry
			                       ? location::absent
			                       : draw_contact(owned, slots, wrists[h].position);
			const auto* item = place == location::back ? owned.next_back() : owned.in_slot(place);
			if (item && place != location::absent)
			{
				const bool support = item->policy.right_control && actor == hand::left;
				hi::offer({actor,
				           {hi::object(hi::domain::carry, item->id),
				            support ? hi::role::support : hi::role::control,
				            hi::button::grip,
				            hi::recipe::single,
				            support ? hi::capability::aim : hi::capability::fire},
				           edge.event,
				           30,
				           0,
				           1,
				           true,
				           true});
			}
		}
	}
	unsigned apply_grips(unsigned claimed)
	{
		if (!batch_ready || !scheduler::is_executing(scheduler::pipeline::server))
			return claimed;
		namespace hi = hand_interaction;
		const auto& input = batch.raw_input;
		const auto& body = batch.frame.body;
		const auto& wrists = batch.frame.wrists;
		const auto valid_hands = batch.frame.valid_hands;
		const auto& slots = batch.frame.holsters;
		const auto& event = batch.edges;
		for (int h = 0; h < 2; ++h)
		{
			const auto which = static_cast<hand>(h);
			if (!(event.pressed & (1u << h)) || (event.released & (1u << h)) || !(valid_hands & (1u << h)) ||
			    !input.squeeze[h].active || !input.squeeze[h].down || owned.in_hand(which))
				continue;
			identity support{}, control{};
			control_attachment attachment = control_attachment::fixed;
			for (const auto& v : owned.instances())
				if (v.at == location::held && v.owner.can_fire() && v.owner.rear != which)
				{
					const auto carried = pose(v, wrists, input, valid_hands);
					if (hi::granted(which, hi::domain::carry, v.id) && carried.authored &&
					    carried.support_available &&
					    underbarrel::ordinary_support_allowed(carried, wrists[h], which) &&
					    support_contact(*carried.authored,
					                    carried.gun,
					                    carried.supports[h],
					                    wrists[static_cast<int>(v.owner.rear)].position,
					                    wrists[h].position,
					                    body.units_per_meter))
					{
						support = v.id;
						break;
					}
				}
			// A carried foregrip weapon exposes its actual authored control point.
			for (const auto& v : owned.instances())
				if (v.at == location::held && v.owner.rear == hand::none &&
				    (!v.policy.right_control || which == hand::right))
				{
					const auto carried = pose(v, wrists, input, valid_hands);
					if (!carried.owner.weapon)
						continue;
					const auto contact = choose_control_contact(carried.gun,
					                                            carried.wrists[h],
					                                            carried.moving_controls[h],
					                                            carried.has_moving_control,
					                                            wrists[h].position,
					                                            body.units_per_meter);
					if (hi::granted(which, hi::domain::carry, v.id) && contact.valid)
					{
						control = v.id;
						attachment = contact.attachment;
						break;
					}
				}
			const auto place = sequences::for_player(batch.player).block_carry
			                       ? location::absent
			                       : draw_contact(owned, slots, batch.hand_positions[h]);
			const auto* item = place == location::back ? owned.next_back() : owned.in_slot(place);
			const auto draw =
			    item && hi::granted(which, hi::domain::carry, item->id) ? place : location::absent;
			const auto claim = claim_grip(owned,
			                              {.actor = which,
			                               .pressed = true,
			                               .released = false,
			                               .available = true,
			                               .support = support,
			                               .control = control,
			                               .slot = draw,
			                               .attachment = attachment});
			if (claim.consumed)
				claimed |= 1u << h;
			if (claim.pose_changed)
			{
				publish();
				invalidate_muzzle();
			}
			complete(claim.change, which);
		}
		return claimed;
	}
	unsigned world_use_hands() noexcept
	{
		if (!batch_ready || !scheduler::is_executing(scheduler::pipeline::server))
			return 0;
		namespace hi = hand_interaction;
		const auto& input = batch.raw_input;
		unsigned available{};
		for (int h = 0; h < 2; ++h)
			if (!owned.in_hand(hand(h)) && !batch.released[h] &&
			    (hi::has(hand(h), hi::domain::world) || (hi::free(hand(h)) && !input.trigger[h].down)))
				available |= 1u << h;
		return available;
	}
	bool pickup(const interaction::target& target, int h)
	{
		if (!batch_ready || !scheduler::is_executing(scheduler::pipeline::server))
			return false;
		const auto which = static_cast<hand>(h);
		const auto item = native_carry::pickup_item({target.key.entity, target.key.generation});
		if (!item.weapon || item.weapon != target.weapon || owned.in_hand(which))
			return false;
		carry::identity recovered;
		if (native_carry::pickup(item, recovered))
		{
			const auto after = native_carry::observe();
			if (after.valid && owned.reconcile_instances({after.owned.data(), after.count}) &&
			    owned.equip(recovered, which))
			{
				preferred = recovered;
				complete({outcome::drawn, preferred}, which);
				return true;
			}
		}
		return false;
	}
	std::span<const instance> interaction_instances() noexcept
	{
		return scheduler::is_executing(scheduler::pipeline::server)
		           ? std::span<const instance>{owned.instances()}
		           : std::span<const instance>{};
	}
	bool release_support(identity id) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server))
			return false;
		const auto* v = owned.find(id);
		if (!v || !valid_hand(v->owner.support))
			return false;
		return owned
		           .release(id,
		                    1u << int(v->owner.support),
		                    location::absent,
		                    false,
		                    [](const instance&) { return false; })
		           .action == outcome::support_released;
	}
	bool acquire_support(identity id, hand actor) noexcept
	{
		return scheduler::is_executing(scheduler::pipeline::server) && owned.support(id, actor);
	}
	bool interaction_hand_occupied(hand actor) noexcept
	{
		return scheduler::is_executing(scheduler::pipeline::server) && owned.in_hand(actor);
	}
	void publish_topology() noexcept
	{
		if (scheduler::is_executing(scheduler::pipeline::server))
		{
			publish();
			invalidate_muzzle();
		}
	}
	void publish_interactions() noexcept
	{
		if (scheduler::is_executing(scheduler::pipeline::server))
			publish();
	}
	void refresh_interaction_objects()
	{
		if (!batch_ready || !scheduler::is_executing(scheduler::pipeline::server))
			return;
		for (size_t i = 0; i < owned.instances().size(); ++i)
			if (owned.instances()[i].at == location::held)
				batch.frame.objects[i] =
				    pose(owned.instances()[i], batch.frame.wrists, batch.raw_input, batch.frame.valid_hands);
	}
	void finish_interactions()
	{
		if (!batch_ready || !scheduler::is_executing(scheduler::pipeline::server))
			return;
		const auto& input = batch.raw_input;
		const auto& body = batch.frame.body;
		const auto& wrists = batch.frame.wrists;
		const auto valid_hands = batch.frame.valid_hands;
		const auto& layout = batch.layout;
		models_frame models;
		models.at = input.sampled_at;
		models.units = body.units_per_meter;
		models.layout = layout;
		models.reference = input.reference_generation;
		std::array<storage_scene, 2> storage{};
		std::size_t stored_count{};
		for (const auto& v : owned.instances())
		{
			if (!v.id)
				continue;
			if (visible_storage(v.at))
			{
				const auto ammo = native_ammunition::observe_carried(batch.player, v.id);
				if (stored_count < storage.size())
					storage[stored_count++] = {v,
					                           layout,
					                           ammo.valid ? ammo.loaded : -1,
					                           input.reference_generation,
					                           input.sampled_at};
				continue;
			}
			// Back/overflow stay hidden. This mesh fallback belongs only to a
			// held secondary gun when independent native viewmodels are disabled.
			if (v.at != location::held || v.id == preferred || models.count >= models.models.size())
				continue;
			const auto model = native_carry::geometry(v.id.weapon);
			if (!model.valid)
				continue;
			const auto held = pose(v, wrists, input, valid_hands);
			if (!held.owner.weapon)
				continue;
			const auto bridge =
			    held.has_muzzle && model.has_muzzle
			        ? hands::pose_math::compose(held.muzzle, hands::pose_math::inverse(model.muzzle))
			        : hands::anchor{};
			for (std::size_t i = 0; i < model.count && models.count < models.models.size(); ++i)
			{
				const auto local = hands::pose_math::compose(bridge, model.parts[i].local);
				models.models[models.count++] = {model.parts[i].model,
				                                 hands::pose_math::compose(held.gun, local),
				                                 v.id,
				                                 local,
				                                 std::size_t(&v - owned.instances().data()) * 32 + i};
			}
		}
		{
			const std::lock_guard lock(publication_mutex);
			render_models = models;
			render_storage = storage;
		}
		independent_fire::update();
		batch_ready = false;
	}

	hold current_hold() noexcept
	{
		const std::lock_guard lock(publication_mutex);
		return presented;
	}
	bool hand_available(hand h) noexcept
	{
		return !active() || hand_interaction::free(h);
	}
	bool hand_has_weapon(hand h) noexcept
	{
		if (!active() || !valid_hand(h))
			return false;
		const std::lock_guard lock(publication_mutex);
		return published.in_hand(h) != nullptr;
	}
	const profile* held_profile(hand h) noexcept
	{
		if (!active())
			return nullptr;
		const std::lock_guard lock(publication_mutex);
		const auto* v = published.in_hand(h);
		const auto* cached = v ? scenes.find(v->id) : nullptr;
		return v && v->owner.rear == h && cached ? cached->authored : nullptr;
	}
	std::optional<hands::quat> support_basis(const hold& owner, std::uint64_t space) noexcept
	{
		if (owner.can_fire() || !valid_hand(owner.support))
			return std::nullopt;
		const std::lock_guard lock(publication_mutex);
		const auto* stored = support_rotations.find(owner.id());
		return stored ? stored->basis(owner, space) : std::nullopt;
	}
	void publish_scene(const scene& value) noexcept
	{
		const std::lock_guard lock(publication_mutex);
		if (const auto* v = published.find(value.owner.id()); v && v->owner.revision == value.owner.revision)
			if (auto* cached = scenes.acquire(v->id))
				*cached = value;
	}
	void publish_hands(const std::array<hands::anchor, 2>& wrists,
	                   const std::array<hands::arm_geometry, 2>& arms,
	                   const controller_input::frame& input,
	                   std::uintptr_t object,
	                   std::uintptr_t matrices,
	                   std::uint32_t epoch,
	                   const hands::vec& view_offset) noexcept
	{
		models_frame models;
		inventory state;
		{
			const std::lock_guard lock(publication_mutex);
			render_hands = {wrists, input.sequence, input.reference_generation, input.sampled_at, arms};
			models = render_models;
			state = published;
			for (int h = 0; h < 2; ++h)
				if (const auto* item = state.in_hand(hand(h)))
					render_hands.owners[h] = item->owner;
		}
		render_sample sample;
		sample.object = object;
		sample.matrices = matrices;
		sample.epoch = epoch;
		sample.reference = input.reference_generation;
		sample.at = input.sampled_at;
		std::array<scene, 2> poses{};
		for (unsigned h = 0; h < 2; ++h)
			if (const auto* v = state.in_hand(static_cast<hand>(h));
			    v && v->owner.holding_hand() == static_cast<hand>(h))
				poses[h] = pose(*v, wrists, input);
		for (std::size_t i = 0; i < models.count && sample.count < sample.parts.size(); ++i)
		{
			const auto& model = models.models[i];
			if (!model.held)
				continue;
			const auto* v = state.find(model.held);
			if (!v || v->at != location::held || !valid_hand(v->owner.holding_hand()))
				continue;
			const auto& solved = poses[static_cast<int>(v->owner.holding_hand())];
			if (solved.owner.id() != model.held)
				continue;
			auto relative = hands::pose_math::compose(solved.gun, model.local);
			relative.position = hands::sub(relative.position, view_offset);
			sample.parts[sample.count++] = {
			    reinterpret_cast<std::uintptr_t>(model.model), model.slot, model.held, v->owner, relative};
		}
		const std::lock_guard lock(publication_mutex);
		render_poses.publish(sample);
	}
	hold held(identity id) noexcept
	{
		const std::lock_guard lock(publication_mutex);
		const auto* v = published.find(id);
		return v && v->at == location::held ? v->owner : hold{};
	}
	bool contains(identity id) noexcept
	{
		const std::lock_guard lock(publication_mutex);
		return published.find(id) != nullptr;
	}
	runtime_lifecycle::ownership_snapshot scene_ownership() noexcept
	{
		const std::lock_guard lock(publication_mutex);
		runtime_lifecycle::ownership_snapshot result;
		result.independent = active();
		for (size_t i = 0; i < published.instances().size(); ++i)
			result.instances[i] = published.instances()[i].id;
		return result;
	}
	bool restore_native_projection() noexcept
	{
		return active() && scheduler::is_executing(scheduler::pipeline::server) &&
		       request_selection(projection().weapon);
	}
	bool request_abdominal(std::uint32_t weapon, hand actor) noexcept
	{
		if (!active() || !scheduler::is_executing(scheduler::pipeline::server) || !weapon ||
		    !valid_hand(actor) || pending_abdominal.weapon || owned.in_hand(actor))
			return false;
		const auto input = controller_input::latest();
		const auto h = unsigned(actor);
		if (!input.squeeze[h].active || !input.squeeze[h].down)
			return false;
		pending_abdominal = {weapon,
		                     actor,
		                     input.reference_generation,
		                     input.squeeze[h].releases,
		                     input.squeeze[h].generation,
		                     controller_input::clock::now()};
		publish();
		return true;
	}
	bool abdominal_active(std::uint32_t weapon) noexcept
	{
		const std::lock_guard lock(publication_mutex);
		if (public_abdominal.weapon == weapon)
			return true;
		const auto* v = published.find_definition(weapon);
		return v && v->at == location::held;
	}
	bool abdominal_request_valid(std::uint32_t weapon) noexcept
	{
		abdominal_request p;
		{
			const std::lock_guard lock(publication_mutex);
			p = public_abdominal;
		}
		if (p.weapon != weapon || p.cancelled || !valid_hand(p.actor))
			return false;
		const auto input = controller_input::latest();
		const auto& grip = input.squeeze[unsigned(p.actor)];
		const auto now = controller_input::clock::now();
		return input.focused && input.reference_generation == p.reference && now >= input.sampled_at &&
		       now - input.sampled_at < 150ms && grip.active && grip.down &&
		       grip.generation == p.generation && grip.releases == p.releases;
	}
	std::uint32_t pending_abdominal_weapon() noexcept
	{
		const std::lock_guard lock(publication_mutex);
		return public_abdominal.weapon;
	}
	identity native_identity(std::uint32_t weapon) noexcept
	{
		if (!active())
			return {weapon, 0};
		return native_ammunition::projected_identity(weapon);
	}
	std::array<instance, 2> held_instances() noexcept
	{
		std::array<instance, 2> result{};
		const std::lock_guard lock(publication_mutex);
		for (unsigned h = 0; h < 2; ++h)
			if (const auto* v = published.in_hand(static_cast<hand>(h));
			    v && v->owner.holding_hand() == static_cast<hand>(h))
				result[h] = *v;
		return result;
	}
	std::array<instance, visible_instance_capacity> visible_instances(bool include_held) noexcept
	{
		const std::lock_guard lock(publication_mutex);
		return visible_instances(published, include_held);
	}
	storage_scene stored_scene(identity id) noexcept
	{
		const std::lock_guard lock(publication_mutex);
		const auto* live = published.find(id);
		if (!live || !visible_storage(live->at))
			return {};
		for (const auto& value : render_storage)
			if (value.item.id == id && value.item.at == live->at &&
			    value.item.owner.revision == live->owner.revision)
				return value;
		return {};
	}
	scene firing_scene(identity id) noexcept
	{
		instance owner;
		hand_frame hands;
		{
			const std::lock_guard lock(publication_mutex);
			const auto* v = published.find(id);
			if (!v || v->at != location::held)
				return {};
			owner = *v;
			hands = render_hands;
			const auto* own = scenes.find(id);
			if (own && own->independent)
				return own->owner.revision == owner.owner.revision ? *own : scene{};
		}
		controller_input::frame sample;
		sample.sequence = hands.sequence;
		sample.reference_generation = hands.reference;
		sample.sampled_at = hands.at;
		if (!hands.sequence)
			return {};
		auto result = pose(owner, hands.wrists, sample);
		result.firing_arm = {};
		const auto h = owner.owner.holding_hand();
		if (valid_hand(h) && hands.owners[unsigned(h)].id() == owner.id &&
		    hands.owners[unsigned(h)].revision == owner.owner.revision)
			result.firing_arm = hands.arms[unsigned(h)];
		return result;
	}
	bool debug_draw(location slot, hand h)
	{
		if (!active() || !scheduler::is_executing(scheduler::pipeline::server) ||
		    !scripted_control::allowed(game::g_entities[0].client) ||
		    sequences::for_player(game::g_entities[0].client).block_carry)
			return false;
		const auto r = owned.draw(slot, h);
		complete(r, h);
		return r.action == outcome::drawn;
	}
	bool debug_release(location target, bool dropping)
	{
		if (cheats::green_beret() && ordinary_slot(target))
			return false;
		if (!active() || !scheduler::is_executing(scheduler::pipeline::server) ||
		    !scripted_control::allowed(game::g_entities[0].client) ||
		    sequences::for_player(game::g_entities[0].client).block_carry)
			return false;
		const auto owner = projection();
		const auto* v = owned.find(owner.id());
		if (!v)
			return false;
		if (dropping)
			return false; // Diagnostic drop uses the native adapter with explicit geometry.
		const auto r = owned.release(v->id, 3, target, true, [](const auto&) { return false; });
		complete(r, owner.rear);
		return r.action == outcome::stowed || r.action == outcome::exchanged;
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			enabled = dvars::register_bool("vr_physicalCarry",
			                               true,
			                               game::DVAR_FLAG_SAVED,
			                               "Grip release, native physical drops, waist/back weapon holsters");
			constexpr std::array names{"vr_holsterWaistWidth",
			                           "vr_holsterWaistDown",
			                           "vr_holsterBackDistance",
			                           "vr_holsterBackDown",
			                           "vr_holsterWaistRadius",
			                           "vr_holsterBackRadius"};
			constexpr std::array defaults{.23f, .60f, .22f, .24f, .16f, .20f};
			for (std::size_t i = 0; i < 6; ++i)
				slot_settings[i] = dvars::register_float(names[i],
				                                         defaults[i],
				                                         .05f,
				                                         1.2f,
				                                         game::DVAR_FLAG_SAVED,
				                                         "Body-relative weapon holster dimensions in meters");
			if (!native_carry::initialize())
			{
				console::error("[VR carry] Native contracts rejected\n");
				return;
			}
			scene_models::on_submit(submit_models);
			scene_models::on_prepare_placement(prepare_held);
			command::add(
			    "vr_carry_status",
			    []
			    {
				    scheduler::once(
				        []
				        {
					        std::ostringstream out;
					        out << "active=" << active() << " selected=" << projection().weapon
					            << " transitions=" << transitions << " rejections=" << rejections
					            << " invariant=" << owned.invariant() << " reason=" << reason
					            << " native=" << native_carry::status() << '\n';
					        out << drop_presentation::status();
					        const auto clips = native_ammunition::instances();
					        out << "physical_clips=" << clips.count() << " capacity=" << clip_ledger::capacity
					            << '\n';
					        for (const auto& clip : clips.entries())
						        if (clip.id)
						        {
							        const auto ammo = native_ammunition::observe_carried(
							            game::g_entities[0].client, clip.id);
							        out << "clip weapon=" << clip.id.weapon
							            << " instance=" << clip.id.generation << " native_key=" << clip.key
							            << " epoch=" << clip.epoch << " projected=" << clip.projected
							            << " valid=" << ammo.valid << " loaded=" << ammo.loaded
							            << " reserve=" << ammo.reserve << '\n';
						        }
					        for (std::size_t n = 0; n < std::min(edge_cursor, edge_history.size()); ++n)
					        {
						        const auto& e = edge_history[(edge_cursor - 1 - n) % edge_history.size()];
						        out << "grip_input seq=" << e.sequence << " valid_poses=" << e.poses
						            << " down=" << e.down << " pressed=" << e.event.pressed
						            << " released=" << e.event.released << " deferred=" << e.event.deferred
						            << " resumed_held=" << e.event.resumed;
						        for (const auto& b : e.buttons)
							        out << " button=" << b.generation << ':' << b.presses << ':'
							            << b.releases;
						        out << '\n';
					        }
					        out << "render_matches=" << render_matches.load()
					            << " render_misses_record=" << render_misses[0].load()
					            << " render_misses_epoch=" << render_misses[1].load()
					            << " render_misses_owner=" << render_misses[2].load()
					            << " render_misses_origin=" << render_misses[3].load() << '\n';
					        out << "holster_presentation=owned_native_skeleton\n";
					        for (std::size_t n = 0; n < std::min(release_cursor, release_history.size()); ++n)
					        {
						        const auto& r =
						            release_history[(release_cursor - 1 - n) % release_history.size()];
						        out << "release weapon=" << r.weapon << " hand=" << r.hand
						            << " mask=" << r.mask << " outcome=" << int(r.action)
						            << " input_age_ms=" << r.input_ms << " work_ms=" << r.work_ms
						            << " detail=" << r.detail << '\n';
					        }
					        for (const auto& v : owned.instances())
						        if (v.id)
							        out << "weapon=" << v.id.weapon << " instance=" << v.id.generation
							            << " location=" << int(v.at) << " rear=" << int(v.owner.rear)
							            << " support=" << int(v.owner.support)
							            << " overflow_order=" << v.overflow_order << '\n';
					        const auto text = out.str();
					        console::info("%s", text.c_str());
					        scheduler::once(
					            [text]
					            { utils::io::write_file_atomic("minidumps/h2-mod-vr-carry.txt", text); },
					            scheduler::pipeline::async);
				        },
				        scheduler::pipeline::server);
			    });
			// The hand interaction runtime owns the server update boundary.
		}
		void pre_destroy() override
		{
			alive = false;
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::carry::component)
