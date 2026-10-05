#include <std_include.hpp>
#include "../../debug_options.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "runtime.hpp"
#include "steering.hpp"
#include "../snowmobile_handle_pose.hpp"
#include "../campaign/scripted_sequences.hpp"
#include <utils/native_memory.hpp>
#include "../hand_interaction/runtime.hpp"
#include "../hands/position_offset.hpp"
#include "../weapon_carry_pose.hpp"
#include "../equipment_runtime.hpp"
#include "../part_hand_constraint.hpp"
#include "../weapons/miniuzi/profile.hpp"
#include "../weapons/g18/profile.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::vehicles
{
	namespace
	{
		namespace hi = hand_interaction;
		namespace w = weapons;
		using namespace vr::gameplay::hands::pose_math;
		using clock = controller_input::clock;
		std::mutex mutex;
		snapshot published{}, state{};
		handle_geometry handle_frame{};
		handle_steering handles;
		equipment::grab_intent intention;
		w::quick_reload::dwell quick;
		w::physical_reload::well_motion magazine_motion;
		controller_input::digital_button_gate trigger;
		controller_input::digital_press_gate dry_trigger;
		std::uint64_t last_sequence{}, next_generation{}, next_press{};
		bool was_active{};
		std::atomic_uint64_t draws{}, returns{}, removals{}, reloads{};
		struct steering_sample
		{
			std::uint64_t sequence{}, reference{};
			std::int64_t time_ms{};
			unsigned held{};
			bool valid{};
			std::array<vec, 2> wrists{};
			vec axis{}, pivot{};
			float raw_angle{}, angle{}, value{}, throttle{};
			unsigned grip_down{}, trigger_down{}, tracked{};
		};
		std::array<steering_sample, 512> steering_history{};
		size_t history_cursor{}, history_count{};
		context history_context{};
		void publish()
		{
			state.handles = handles.held();
			state.steering = handles.value();
			state.steering_raw_angle = handles.raw_angle();
			state.steering_angle = handles.angle();
			state.steering_axis = handles.axis();
			state.steering_pivot = handles.pivot();
			state.steering_sample_valid = handles.sample_valid();
			if (!state.handles || !state.steering_sample_valid)
				state.throttle = 0;
			const std::lock_guard lock(mutex);
			published = state;
			if (!debug_options::enabled(debug_options::probe::vehicle))
				return;
			if (history_context.epoch != state.driving.epoch ||
			    history_context.entity != state.driving.entity)
			{
				history_cursor = history_count = 0;
				history_context = state.driving;
			}
			if (state.driving.type == kind::snowmobile && state.sequence &&
			    (!history_count ||
			     steering_history[(history_cursor + steering_history.size() - 1) % steering_history.size()]
			             .sequence != state.sequence))
			{
				steering_history[history_cursor++ % steering_history.size()] = {
				    state.sequence,
				    state.reference,
				    std::chrono::duration_cast<std::chrono::milliseconds>(state.at.time_since_epoch())
				        .count(),
				    state.handles,
				    state.steering_sample_valid,
				    state.steering_hands,
				    state.steering_axis,
				    state.steering_pivot,
				    state.steering_raw_angle,
				    state.steering_angle,
				    state.steering,
				    state.throttle,
				    state.steering_grip_down,
				    state.steering_trigger_down,
				    state.steering_tracked};
				history_count = std::min(history_count + 1, steering_history.size());
			}
		}
		void stow()
		{
			if (state.owner.can_fire())
				++returns;
			state.owner.rear = state.owner.support = vr::hand::none;
			state.owner.rear_revision = ++state.owner.revision;
			state.magazine_grabbed = false;
			state.magazine_hand = vr::hand::none;
			state.held_rounds = 0;
			magazine_motion = {};
			state.press = {};
			state.fire = state.quick_loading = false;
			state.pull = 0;
			quick.reset();
			trigger = {};
			dry_trigger = {};
			intention.reset();
		}
		hi::target target(unsigned part = 0)
		{
			return hi::object(hi::domain::vehicle, state.owner.id(), part);
		}
		void report()
		{
			for (unsigned h = 0; h < 2; ++h)
				if (handles.held() & (1u << h))
					hi::observed(vr::hand(h),
					             {target(3 + h),
					              hi::role::foregrip,
					              hi::button::grip,
					              hi::recipe::single,
					              hi::capability::action});
			if (state.owner.can_fire())
				hi::observed(state.owner.rear,
				             {target(),
				              hi::role::control,
				              hi::button::grip,
				              hi::recipe::single,
				              hi::capability::fire});
			if (vr::valid_hand(state.owner.support))
				hi::observed(state.owner.support,
				             {target(1),
				              hi::role::support,
				              hi::button::grip,
				              hi::recipe::single,
				              hi::capability::aim});
			if (vr::valid_hand(state.magazine_hand))
				hi::observed(state.magazine_hand,
				             {target(2),
				              state.magazine_grabbed ? hi::role::part : hi::role::supply,
				              hi::button::trigger,
				              hi::recipe::single,
				              {}});
		}
		w::carry::holster_layout layout()
		{
			w::carry::holster_layout result;
			const std::array<const char*, 6> names{"vr_holsterWaistWidth",
			                                       "vr_holsterWaistDown",
			                                       "vr_holsterBackDistance",
			                                       "vr_holsterBackDown",
			                                       "vr_holsterWaistRadius",
			                                       "vr_holsterBackRadius"};
			std::array<float*, 6> values{&result.waist_width,
			                             &result.waist_down,
			                             &result.back_distance,
			                             &result.back_down,
			                             &result.waist_radius,
			                             &result.back_radius};
			for (unsigned i = 0; i < names.size(); ++i)
				if (const auto* value = game::Dvar_FindVar(names[i]))
					*values[i] = value->current.value;
			return result;
		}
		bool same(context a, context b)
		{
			return a.type == b.type && a.entity == b.entity && a.epoch == b.epoch;
		}
		unsigned token(kind type)
		{
			for (unsigned i = 1; i < 512; ++i)
			{
				const auto* def = game::weapon_defs[i];
				const char* name{};
				std::array<char, 64> text{};
				if (def && utils::native_memory::read_bytes(&name, def, sizeof(name)) && name &&
				    utils::native_memory::read_bytes(text.data(), name, text.size() - 1) &&
				    std::string_view(text.data()) == weapon_name(type))
					return i;
			}
			return 0;
		}
	}
	const w::profile& profile(kind type) noexcept
	{
		return type == kind::zodiac ? w::miniuzi::base : w::g18::base;
	}
	context current() noexcept
	{
		if (!game::CL_IsCgameInitialized())
			return {};
		const auto s = sequences::for_player(game::CG_GetPredictedPlayerState(0));
		return s.scene == sequences::scenario::vehicle && s.stage == sequences::phase::transport
		           ? context{s.vehicle, s.linked_entity, s.epoch}
		           : context{};
	}
	bool active() noexcept
	{
		const auto* hands = game::Dvar_FindVar("vr_independentHands");
		return hands && hands->current.enabled && head_pose_bridge::get_status().enabled && bool(current());
	}
	bool presentation_allowed() noexcept
	{
		const auto c = current();
		return bool(c) && native::ready(c.type) && assets_ready(c.type) && active();
	}
	snapshot latest() noexcept
	{
		const std::lock_guard lock(mutex);
		return published;
	}
	void publish_handles(const handle_geometry& frame) noexcept
	{
		const std::lock_guard lock(mutex);
		handle_frame = frame;
	}
	handle_geometry rendered_handles() noexcept
	{
		const std::lock_guard lock(mutex);
		return handle_frame;
	}
	driver_input controls(const controller_input::frame& input, clock::time_point now) noexcept
	{
		const auto s = latest();
		if (!s.handles || !s.steering_sample_valid || s.driving.type != kind::snowmobile ||
		    !same(s.driving, current()) || !input.focused || input.orientation_settling ||
		    input.reference_generation != s.reference || input.continuity_generation != s.continuity ||
		    now < input.sampled_at || now - input.sampled_at > 150ms || now < s.at || now - s.at > 150ms)
			return {};
		return physical_driver_input(s.handles, s.steering, input);
	}
	void constrain_hands(std::array<anchor, 2>& targets,
	                     const std::array<quat, 2>& basis,
	                     vec offset,
	                     std::uint64_t reference) noexcept
	{
		const auto s = latest();
		const auto g = rendered_handles();
		const auto now = clock::now();
		head_pose_bridge::spatial_frame body;
		if (!s.handles || !same(s.driving, current()) || !same(s.driving, g.driving) ||
		    s.reference != reference || g.reference != reference || now < s.at || now - s.at > 150ms ||
		    now < g.at || now - g.at > 150ms || !head_pose_bridge::get_spatial_frame(body) ||
		    body.generation != reference || now < body.captured_at || now - body.captured_at > 150ms)
			return;
		const auto handle = compose({body.world_origin, from_axis(body.world_yaw_axis)}, g.handle);
		for (unsigned h = 0; h < 2; ++h)
			if (s.handles & (1u << h))
			{
				const auto wrist = compose(handle, snowmobile::wrists[h]);
				targets[h] = {sub(wrist.position, offset),
				              normalize(multiply(wrist.rotation, conjugate(basis[h])))};
			}
	}
	void reconcile_lifecycle() noexcept
	{
		if (was_active && !active())
			reset();
	}
	void reset() noexcept
	{
		state = {};
		handles.reset();
		publish_handles({});
		quick.reset();
		magazine_motion = {};
		trigger = {};
		dry_trigger = {};
		intention.reset();
		last_sequence = 0;
		was_active = false;
		publish();
	}
	void publish_geometry(kind type,
	                      const std::array<anchor, 2>& controls,
	                      const std::array<quat, 2>& basis,
	                      const std::array<quat, 2>& mirror,
	                      bool handle_pose_ready) noexcept
	{
		const std::lock_guard lock(mutex);
		if (published.driving.type != type)
			return;
		published.controls = controls;
		published.basis = basis;
		published.mirror = mirror;
		published.geometry_ready = true;
		published.handle_pose_ready = handle_pose_ready;
	}
	anchor gun_pose(const snapshot& s, const std::array<anchor, 2>& wrists) noexcept
	{
		if (!s.owner.can_fire())
			return {};
		const auto h = unsigned(s.owner.rear);
		auto rotation = wrists[h].rotation;
		const auto& p = profile(s.driving.type);
		if (vr::valid_hand(s.owner.support))
		{
			const auto other = unsigned(s.owner.support);
			const auto local = w::carry::support_grip(p, other, s.mirror[other]);
			rotation = w::aimed_rotation(p.aiming,
			                             rotation,
			                             wrists[h].position,
			                             wrists[other].position,
			                             sub(local.position, s.controls[h].position));
		}
		return {sub(wrists[h].position, rotate(rotation, s.controls[h].position)), rotation};
	}
	anchor magazine_pose(const snapshot& s, const std::array<anchor, 2>& wrists) noexcept
	{
		const auto& p = *profile(s.driving.type).reload;
		if (!vr::valid_hand(s.magazine_hand) || s.magazine_grabbed)
			return compose(s.gun, p.magazine_rest);
		const auto h = unsigned(s.magazine_hand);
		auto wrist = wrists[h];
		wrist.rotation = normalize(multiply(wrist.rotation, s.basis[h]));
		const auto local = h == 0 ? p.magazine_in_wrist
		                          : vr::gameplay::hands::pose_mirror::object_in_wrist(
		                                p.magazine_rest, p.magazine_in_wrist, s.mirror[h]);
		return compose(wrist, local);
	}
	bool update_interactions() noexcept
	{
		const auto driving = current();
		if (!driving || !active())
		{
			if (was_active)
			{
				reset();
				hi::begin({});
				hi::finish();
				hi::suspend();
			}
			return false;
		}
		was_active = true;
		try
		{
			if (!same(driving, state.driving))
			{
				stow();
				handles.reset();
				state = {};
				state.driving = driving;
				state.owner.weapon = token(driving.type);
				state.owner.instance_generation = ++next_generation;
				state.owner.revision = state.owner.rear_revision = 1;
				state.rounds = native::rounds(driving);
				state.inserted = true;
				last_sequence = 0;
				hi::suspend();
			}
			const auto geometry = latest();
			if (same(driving, geometry.driving))
			{
				state.controls = geometry.controls;
				state.basis = geometry.basis;
				state.mirror = geometry.mirror;
				state.geometry_ready = geometry.geometry_ready;
				state.handle_pose_ready = geometry.handle_pose_ready;
			}
			const auto input = controller_input::latest();
			const auto now = clock::now();
			const auto head = head_pose_bridge::get_status();
			hi::frame frame;
			frame.input = input;
			const auto* paused = game::Dvar_FindVar("cl_paused");
			const bool usable =
			    native::ready(driving.type) && state.owner.weapon && input.focused &&
			    !input.orientation_settling && head.pose_available && !head.recenter_pending &&
			    !*game::keyCatchers && paused && !paused->current.integer && now >= input.sampled_at &&
			    now - input.sampled_at <= 150ms && head_pose_bridge::get_spatial_frame(frame.body) &&
			    frame.body.generation == input.reference_generation && now >= frame.body.captured_at &&
			    now - frame.body.captured_at <= 150ms;
			if (!usable)
			{
				stow();
				handles.reset();
				state.handles = 0;
				state.steering = 0;
				hi::begin({});
				hi::finish();
				hi::suspend();
				publish();
				return true;
			}
			if (state.reference &&
			    (state.reference != input.reference_generation ||
			     state.continuity != input.continuity_generation || input.sequence < last_sequence))
			{
				stow();
				handles.reset();
				hi::suspend();
				last_sequence = 0;
			}
			if (input.sequence == last_sequence)
				return true;
			last_sequence = input.sequence;
			state.at = input.sampled_at;
			state.reference = input.reference_generation;
			state.continuity = input.continuity_generation;
			state.sequence = input.sequence;
			state.units = frame.body.units_per_meter;
			hands::position_offsets offsets;
			if (const auto* v = game::Dvar_FindVar("vr_handOffsetInward"))
				offsets.inward_meters = v->current.value;
			if (const auto* v = game::Dvar_FindVar("vr_handOffsetBack"))
				offsets.back_meters = v->current.value;
			if (const auto* v = game::Dvar_FindVar("vr_handOffsetUp"))
				offsets.up_meters = v->current.value;
			for (unsigned h = 0; h < 2; ++h)
				if (tracked_wrist(input, frame.body, {}, int(h), offsets, frame.wrists[h]))
					frame.valid_hands |= 1u << h;
			frame.holsters = w::carry::locate_holsters(frame.body, layout());
			hi::begin(frame, true);
			if (state.owner.can_fire() && !(frame.valid_hands & (1u << unsigned(state.owner.rear))))
				stow();
			unsigned released{}, pressed{}, available{};
			for (unsigned h = 0; h < 2; ++h)
			{
				const auto edge = hi::input(vr::hand(h), hi::button::grip);
				if (edge.release || (input.squeeze[h].active && !input.squeeze[h].down))
					released |= 1u << h;
				if (edge.press)
					pressed |= 1u << h;
			}
			handles.release(released);
			const auto bars = rendered_handles();
			const bool bars_ready = driving.type == kind::snowmobile && state.handle_pose_ready &&
			                        same(driving, bars.driving) &&
			                        bars.reference == input.reference_generation && now >= bars.at &&
			                        now - bars.at <= 150ms && std::isfinite(state.units) && state.units > 0;
			std::array<vec, 2> local_hands{};
			const anchor reference{frame.body.world_origin, from_axis(frame.body.world_yaw_axis)};
			unsigned steering_valid{};
			// Steering consumes physical motion, independently of camera/hand visual
			// stabilization. The common bar angle has its own shared adaptive filter.
			auto physical_body = frame.body;
			physical_body.head_stabilized = false;
			for (unsigned h = 0; h < 2; ++h)
			{
				anchor wrist;
				if (tracked_wrist(input, physical_body, {}, int(h), offsets, wrist, true))
				{
					local_hands[h] = scale(compose(inverse(reference), wrist).position, 1 / state.units);
					steering_valid |= 1u << h;
				}
			}
			state.steering_hands = local_hands;
			state.steering_tracked = steering_valid;
			state.steering_grip_down = state.steering_trigger_down = 0;
			for (unsigned h = 0; h < 2; ++h)
			{
				if (input.squeeze[h].active && input.squeeze[h].down)
					state.steering_grip_down |= 1u << h;
				if (input.trigger[h].active && input.trigger[h].down)
					state.steering_trigger_down |= 1u << h;
			}
			const auto tracking_up =
			    pose_filter::rotate(pose_filter::transpose(frame.body.reference.orientation), {0, 1, 0});
			const auto hardware = handle_control_geometry(bars.handle,
			                                              snowmobile::wrists,
			                                              state.units,
			                                              {-tracking_up[2], -tracking_up[0], tracking_up[1]});
			unsigned maintained = steering_valid;
			for (unsigned h = 0; h < 2; ++h)
				if (!input.squeeze[h].active || !input.squeeze[h].down)
					maintained &= ~(1u << h);
			(void)handles.update(local_hands, maintained, input.sampled_at);
			if (handles.held() && (steering_valid & handles.held()) == handles.held())
				for (unsigned h = 0; h < 2; ++h)
					if ((handles.held() & (1u << h)) && hi::input(vr::hand(h), hi::button::secondary).press)
					{
						handles.center(local_hands);
						break;
					}
			if (vr::valid_hand(state.owner.support) && (released & (1u << unsigned(state.owner.support))))
			{
				state.owner.support = vr::hand::none;
				++state.owner.revision;
			}
			if (state.owner.can_fire() && (released & (1u << unsigned(state.owner.rear))))
			{
				if (vr::valid_hand(state.owner.support))
				{
					state.owner.rear = state.owner.support;
					state.owner.support = vr::hand::none;
					state.owner.rear_revision = ++state.owner.revision;
					state.press = {};
					quick.reset();
					trigger = {};
					dry_trigger = {};
				}
				else
					stow();
			}
			if (vr::valid_hand(state.magazine_hand))
			{
				const auto h = unsigned(state.magazine_hand);
				const auto edge = hi::input(vr::hand(h), hi::button::trigger);
				if (edge.release || !input.trigger[h].down || !(frame.valid_hands & (1u << h)))
				{
					state.magazine_hand = vr::hand::none;
					state.magazine_grabbed = false;
					state.held_rounds = 0;
					state.pull = 0;
					magazine_motion = {};
				}
			}
			report();
			hi::synchronize();
			for (unsigned h = 0; h < 2; ++h)
				if (hi::free(vr::hand(h)))
					available |= 1u << h;
			const auto pending = intention.consume(input, available & frame.valid_hands, pressed, released);
			const auto& p = profile(driving.type);
			const auto& reload = *p.reload;
			if (state.owner.can_fire())
				state.gun = gun_pose(state, frame.wrists);
			for (unsigned h = 0; h < 2; ++h)
			{
				if (!(available & frame.valid_hands & (1u << h)) || !state.geometry_ready)
					continue;
				const auto actor = vr::hand(h);
				const auto edge = hi::input(actor, hi::button::trigger);
				if (bars_ready && (steering_valid & (1u << h)) && (pressed & (1u << h)) &&
				    input.squeeze[h].down)
				{
					const auto contact = compose(reference, compose(bars.handle, snowmobile::wrists[h]));
					const auto distance = length(sub(frame.wrists[h].position, contact.position)) /
					                      (snowmobile::grip.acquire_meters * state.units);
					if (distance <= 1)
						hi::offer({actor,
						           {target(3 + h),
						            hi::role::foregrip,
						            hi::button::grip,
						            hi::recipe::single,
						            hi::capability::action},
						           hi::input(actor, hi::button::grip).event,
						           10,
						           distance,
						           1,
						           true,
						           true});
				}
				if (!state.owner.can_fire() && (pending & (1u << h)))
				{
					const auto distance = weapon_grab_distance(frame.body, frame.wrists[h].position);
					if (distance <= 1)
						hi::offer({actor,
						           {target(),
						            hi::role::control,
						            hi::button::grip,
						            hi::recipe::single,
						            hi::capability::fire},
						           hi::input(actor, hi::button::grip).event,
						           20,
						           distance,
						           1,
						           true,
						           true});
				}
				if (!state.owner.can_fire())
					continue;
				if (edge.press && state.inserted && !vr::valid_hand(state.magazine_hand))
				{
					const auto& c = *reload.magazine_contacts;
					auto wrist = frame.wrists[h];
					wrist.rotation = normalize(multiply(wrist.rotation, state.basis[h]));
					const auto contact = compose(wrist,
					                             {h == 0 ? c.grip_contact
					                                     : vr::gameplay::hands::pose_mirror::local_point(
					                                           c.grip_contact, state.mirror[h]),
					                              {0, 0, 0, 1}})
					                         .position;
					const auto local = compose(inverse(state.gun), {contact, {0, 0, 0, 1}}).position;
					const auto distance = w::physical_reload::box_distance(local, c.grab_low, c.grab_high) /
					                      (frame.body.units_per_meter * magazine_grab_margin);
					if (distance <= 1)
						hi::offer({actor,
						           {target(2), hi::role::part, hi::button::trigger, hi::recipe::single, {}},
						           edge.event,
						           10,
						           distance,
						           1,
						           true,
						           true});
				}
				if (edge.press && edge.down && !vr::valid_hand(state.magazine_hand))
				{
					const auto distance =
					    magazine_supply_distance(frame.body, frame.holsters, frame.wrists[h].position);
					if (distance <= 1)
						hi::offer({actor,
						           {target(2), hi::role::supply, hi::button::trigger, hi::recipe::single, {}},
						           edge.event,
						           20,
						           distance,
						           1,
						           true,
						           true});
				}
				if ((pressed & (1u << h)) && !input.trigger[h].down)
				{
					const auto local = w::carry::support_grip(p, h, state.mirror[h]);
					const auto distance =
					    length(sub(frame.wrists[h].position, compose(state.gun, local).position)) /
					    (std::max(p.acquire_meters, support_grab_margin) * state.units);
					if (distance <= 1)
						hi::offer({actor,
						           {target(1),
						            hi::role::support,
						            hi::button::grip,
						            hi::recipe::single,
						            hi::capability::aim},
						           hi::input(actor, hi::button::grip).event,
						           20,
						           distance,
						           1,
						           true,
						           true});
				}
			}
			hi::resolve();
			for (unsigned h = 0; h < 2; ++h)
			{
				const auto actor = vr::hand(h);
				if (bars_ready &&
				    hi::granted(
				        actor, hi::domain::vehicle, state.owner.id(), hi::button::grip, hi::role::foregrip))
					handles.grab(h, local_hands, hardware);
				if (!state.owner.can_fire() &&
				    hi::granted(
				        actor, hi::domain::vehicle, state.owner.id(), hi::button::grip, hi::role::control))
				{
					state.owner.rear = actor;
					state.owner.source = w::hold_source::interaction;
					state.owner.rear_revision = ++state.owner.revision;
					quick.reset();
					trigger = {};
					dry_trigger = {};
					++draws;
				}
				else if (state.owner.can_fire() && !vr::valid_hand(state.owner.support) &&
				         hi::granted(actor,
				                     hi::domain::vehicle,
				                     state.owner.id(),
				                     hi::button::grip,
				                     hi::role::support))
				{
					state.owner.support = actor;
					++state.owner.revision;
				}
				if (state.owner.can_fire() && state.inserted && !vr::valid_hand(state.magazine_hand) &&
				    hi::granted(
				        actor, hi::domain::vehicle, state.owner.id(), hi::button::trigger, hi::role::part))
				{
					state.magazine_hand = actor;
					state.magazine_grabbed = true;
					state.grab_start = state.grab_last =
					    compose(inverse(state.gun), frame.wrists[h]).position;
					state.pull = 0;
					magazine_motion = {};
				}
				else if (state.owner.can_fire() && !vr::valid_hand(state.magazine_hand) &&
				         hi::granted(actor,
				                     hi::domain::vehicle,
				                     state.owner.id(),
				                     hi::button::trigger,
				                     hi::role::supply))
				{
					state.magazine_hand = actor;
					state.magazine_grabbed = false;
					state.held_rounds = 32;
					state.pull = 0;
					magazine_motion = {};
					magazine_motion.withdraw = true;
					native::feedback(state, w::mechanics::effect::magazine_take);
				}
			}
			state.fire = false;
			if (state.owner.can_fire())
			{
				state.gun = gun_pose(state, frame.wrists);
				const int observed = native::rounds(driving);
				if (observed >= 0)
					state.rounds = observed;
				const auto remove_magazine = [&]
				{
					if (!state.inserted || observed < 0 || !native::set_rounds(driving, observed, 0))
						return false;
					state.inserted = false;
					if (state.magazine_grabbed)
					{
						state.held_rounds = observed;
						magazine_motion = {};
						magazine_motion.withdraw = true;
					}
					state.magazine_grabbed = false;
					state.rounds = 0;
					state.pull = 0;
					state.press = {};
					++removals;
					native::feedback(state, w::mechanics::effect::magazine_take);
					return true;
				};
				// Same holding-hand secondary edge as ordinary B/Y magazine release.
				// A previously pinched magazine stays in the offhand; otherwise it
				// leaves the feed. No chamber/slide state exists in the driver adapter.
				if (hi::input(state.owner.rear, hi::button::secondary).press)
					(void)remove_magazine();
				if (state.magazine_grabbed && state.inserted)
				{
					const auto local =
					    compose(inverse(state.gun), frame.wrists[unsigned(state.magazine_hand)]).position;
					const bool continuous = length(sub(local, state.grab_last)) / state.units <=
					                        reload.interaction.max_contact_step;
					state.grab_last = local;
					if (!continuous)
					{
						state.magazine_hand = vr::hand::none;
						state.magazine_grabbed = false;
						state.pull = 0;
					}
					const auto delta = scale(sub(local, state.grab_start), 1 / state.units);
					const auto axis = rotate(reload.well.rotation, {0, 0, -1});
					state.pull = std::max(0.f, dot(delta, axis));
					const auto* manual = reload.interaction.manual_magazine;
					const float travel = manual ? manual->pull_travel : .04f,
					            lateral = manual ? manual->pull_lateral_limit : .10f;
					if (continuous && state.pull >= travel &&
					    length(sub(delta, scale(axis, state.pull))) <= lateral)
						(void)remove_magazine();
				}
				state.magazine = magazine_pose(state, frame.wrists);
				if (vr::valid_hand(state.magazine_hand) && !state.magazine_grabbed)
				{
					// Same swept mouth/alignment and withdrawal rules as physical reload.
					const auto tip = w::magazine_tip_in_well(reload, state.gun, state.magazine, state.units);
					const auto contact = w::physical_reload::advance_well(
					    reload.interaction,
					    magazine_motion,
					    tip,
					    w::magazine_alignment(reload, state.gun, state.magazine),
					    state.inserted);
					if (contact.insert && observed >= 0 &&
					    native::set_rounds(driving, observed, state.held_rounds))
					{
						state.inserted = true;
						state.rounds = state.held_rounds;
						state.held_rounds = 0;
						state.magazine_hand = vr::hand::none;
						magazine_motion = {};
						quick.reset();
						++reloads;
						native::feedback(state, w::mechanics::effect::magazine_in);
					}
				}
				const auto zone = reload_zone(
				    frame.body, frame.holsters, frame.wrists[unsigned(state.owner.rear)].position);
				if (quick.update(state.owner,
				                 state.owner.instance_generation,
				                 input,
				                 zone,
				                 !state.inserted && observed >= 0,
				                 now,
				                 true) &&
				    native::set_rounds(driving, observed, 32))
				{
					state.inserted = true;
					state.rounds = 32;
					quick.reset();
					++reloads;
					native::feedback(state, w::mechanics::effect::magazine_in);
				}
				if (quick.began())
					native::feedback(state, w::mechanics::effect::none, true);
				state.quick_loading = quick.active(state.owner, input, now);
				const auto& trigger_input = input.trigger[unsigned(state.owner.rear)];
				const bool dry_press = dry_trigger.consume(trigger_input);
				if (!state.inserted || state.rounds <= 0 || state.magazine_grabbed || state.quick_loading)
					state.press = {};
				else if (dry_press)
					state.press = {++next_press, now};
				state.fire = trigger.consume(trigger_input) && state.inserted && state.rounds > 0 &&
				             !state.magazine_grabbed && !state.quick_loading;
				if (dry_press && (!state.inserted || !state.rounds) && !state.quick_loading &&
				    !state.magazine_grabbed)
					native::feedback(state, w::mechanics::effect::dry_fire);
				state.magazine = magazine_pose(state, frame.wrists);
			}
			state.throttle = physical_driver_input(handles.held(), handles.value(), input).throttle;
			report();
			hi::finish();
			++state.revision;
			publish();
			native::update_aim(state);
			return true;
		}
		catch (...)
		{
			stow();
			handles.reset();
			state.handles = 0;
			state.steering = 0;
			hi::suspend();
			publish();
			return true;
		}
	}
	std::array<hands::pose_library, 2> bind_hands(const rig& r,
	                                              std::span<const bone_definition> bones) noexcept
	{
		std::array<hands::pose_library, 2> result;
		for (unsigned i = 0; i < 2; ++i)
		{
			auto p = profile(i == 0 ? kind::zodiac : kind::snowmobile);
			p.equip_rest = {};
			result[i] = bind_weapon_poses(r, bones, p);
		}
		return result;
	}
	hands::pose_library bind_handle_hands(const rig& r, std::span<const bone_definition> bones) noexcept
	{
		return bind_weapon_poses(r, bones, snowmobile::grip);
	}
	void present(const hands::interaction_rig& parts,
	             const rig& r,
	             const controller_input::frame& input,
	             const std::array<anchor, 2>& wrists,
	             const std::array<vec, 2>& shoulders,
	             const std::array<vec, 3>& axes,
	             float units,
	             std::span<bone> solved) noexcept
	{
		(void)units;
		auto s = latest();
		if (!presentation_allowed() || !s.driving)
			return;
		const auto& p = profile(s.driving.type);
		const auto& library = parts.vehicle_poses[s.driving.type == kind::zodiac ? 0 : 1];
		if (!library.valid)
			return;
		std::array<quat, 2> mirrors;
		std::array<anchor, 2> controls;
		for (unsigned h = 0; h < 2; ++h)
		{
			mirrors[h] = library.mirror_basis[r.arms[h].wrist];
			controls[h] = w::carry::control_grip(p, int(h), mirrors[h]);
		}
		publish_geometry(s.driving.type, controls, parts.basis, mirrors, parts.vehicle_handles.valid);
		s.controls = controls;
		s.mirror = mirrors;
		s.basis = parts.basis;
		if (s.reference != input.reference_generation)
			return;
		if (s.handles && parts.vehicle_handles.valid)
			for (unsigned h = 0; h < 2; ++h)
				if (s.handles & (1u << h))
					fingers(r, parts.vehicle_handles, snowmobile::grip, snowmobile::fingers, int(h), solved);
		if (!s.owner.can_fire())
			return;
		s.gun = gun_pose(s, wrists);
		w::carry::pose_profile adapted(p, s.owner, r, library);
		for (unsigned h = 0; h < 2; ++h)
		{
			if (vr::hand(h) == s.owner.rear || vr::hand(h) == s.owner.support)
			{
				const auto local = vr::hand(h) == s.owner.rear ? controls[h] : adapted.supports[h];
				const auto desired = compose(s.gun, local);
				if (vr::hand(h) == s.owner.support)
					(void)w::constrain_part_hand(r,
					                             library,
					                             adapted.value,
					                             wrists,
					                             shoulders,
					                             axes,
					                             int(s.owner.rear),
					                             desired,
					                             solved);
				else
					move_part(r, r.arms[h].wrist, desired, solved);
				fingers(r, library, p, adapted.value.fingers, int(h), solved);
			}
			else if (vr::hand(h) == s.magazine_hand)
			{
				auto wrist = wrists[h];
				wrist.rotation = normalize(multiply(wrist.rotation, parts.basis[h]));
				move_part(r, r.arms[h].wrist, wrist, solved);
				vr::gameplay::hands::pose_mirror::fingers(
				    r, library, p, p.reload->magazine_fingers, int(h), solved, h == 1);
			}
		}
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			scripting::on_level_start(reset);
			scripting::on_shutdown(
			    [](bool, bool after)
			    {
				    if (!after)
					    reset();
			    });
			command::add(
			    "vr_vehicle_steering_trace",
			    []
			    {
				    std::array<steering_sample, 512> samples;
				    size_t cursor{}, count{};
				    context driving;
				    {
					    const std::lock_guard lock(mutex);
					    samples = steering_history;
					    cursor = history_cursor;
					    count = history_count;
					    driving = history_context;
				    }
				    std::string csv =
				        "entity,epoch,time_ms,sequence,reference,held,angle_valid,raw_angle_deg,filtered_angle_deg,steering,left_x_m,left_y_m,left_z_m,right_x_m,right_y_m,right_z_m,axis_x,axis_y,axis_z,pivot_x_m,pivot_y_m,pivot_z_m,throttle,grip_down,trigger_down,tracked\n";
				    for (size_t i = 0; i < count; ++i)
				    {
					    const auto& s = samples[(cursor + samples.size() - count + i) % samples.size()];
					    csv += std::format(
					        "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
					        driving.entity,
					        driving.epoch,
					        s.time_ms,
					        s.sequence,
					        s.reference,
					        s.held,
					        s.valid,
					        s.raw_angle,
					        s.angle,
					        s.value,
					        s.wrists[0][0],
					        s.wrists[0][1],
					        s.wrists[0][2],
					        s.wrists[1][0],
					        s.wrists[1][1],
					        s.wrists[1][2],
					        s.axis[0],
					        s.axis[1],
					        s.axis[2],
					        s.pivot[0],
					        s.pivot[1],
					        s.pivot[2],
					        s.throttle,
					        s.grip_down,
					        s.trigger_down,
					        s.tracked);
				    }
				    utils::io::write_file_atomic("minidumps/h2-mod-vr-steering.csv", csv);
				    console::print_text(
				        console::con_type_info,
				        std::format("Saved {} steering samples to minidumps/h2-mod-vr-steering.csv\n",
				                    count));
			    });
			command::add(
			    "vr_vehicle_status",
			    []
			    {
				    const auto s = latest();
				    const auto report =
				        std::format(
				            "vehicle={} entity={} epoch={} native_ready={} assets_ready={} geometry_ready={} holder={} support={} inserted={} rounds={} magazine_hand={} quick_loading={} fire={} draws={} returns={} removals={} reloads={}\n",
				            int(s.driving.type),
				            s.driving.entity,
				            s.driving.epoch,
				            native::ready(s.driving.type),
				            assets_ready(s.driving.type),
				            s.geometry_ready,
				            int(s.owner.rear),
				            int(s.owner.support),
				            s.inserted,
				            s.rounds,
				            int(s.magazine_hand),
				            s.quick_loading,
				            s.fire,
				            draws.load(),
				            returns.load(),
				            removals.load(),
				            reloads.load()) +
				        native::status() + render_status();
				    const auto complete =
				        report +
				        std::format(
				            "handles={} physical_steering={} throttle={} grip_down={} trigger_down={} tracked={} raw_angle_deg={} filtered_angle_deg={} angle_valid={} input_sequence={}\n"
				            "steering_axis=({},{},{}) pivot_m=({},{},{}) left_m=({},{},{}) right_m=({},{},{})\n",
				            s.handles,
				            s.steering,
				            s.throttle,
				            s.steering_grip_down,
				            s.steering_trigger_down,
				            s.steering_tracked,
				            s.steering_raw_angle,
				            s.steering_angle,
				            s.steering_sample_valid,
				            s.sequence,
				            s.steering_axis[0],
				            s.steering_axis[1],
				            s.steering_axis[2],
				            s.steering_pivot[0],
				            s.steering_pivot[1],
				            s.steering_pivot[2],
				            s.steering_hands[0][0],
				            s.steering_hands[0][1],
				            s.steering_hands[0][2],
				            s.steering_hands[1][0],
				            s.steering_hands[1][1],
				            s.steering_hands[1][2]);
				    console::print_text(console::con_type_info, complete);
				    utils::io::write_file_atomic("minidumps/h2-mod-vr-vehicle.txt", complete);
			    });
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::vehicles::component)
