#pragma once
#include "../physical_reload_configuration.hpp"
#include "../detachable_magazine.hpp"
#include "../part_grip_pose.hpp"
#include "../receiver_bolt_release.hpp"

namespace vr::gameplay::weapons::families::ar
{
	// Shared, explicitly selected M4/M16 policy. This is not native admission.
	inline constexpr mechanics::rules reload_rules{.magazine_capacity = 30,
	                                               .release = mechanics::magazine_release::button,
	                                               .last_round_lock = true,
	                                               .release_control = true,
	                                               .plus_one = true};
	struct charging_handle_configuration
	{
		float stroke_m{};
		float full_stroke_m{};
		const physical_reload::receiver_bolt_release* receiver_release{};
	};
	constexpr physical_reload::profile charging_handle(const charging_handle_configuration& action) noexcept
	{
		return {.waist_radius = physical_reload::defaults::waist_radius_m,
		        .slide_radius = part_grip_capture::radius_m,
		        .slide_stroke = action.stroke_m,
		        .locked_travel = 0.f,
		        .full_stroke = action.full_stroke_m,
		        .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
		        .well_radius = physical_reload::defaults::well_radius_m,
		        .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
		        .insertion_cosine = physical_reload::defaults::insertion_cosine,
		        .max_contact_step = physical_reload::defaults::max_contact_step_m,
		        .slide_axis = physical_reload::defaults::rearward_axis,
		        .well_capture_below = physical_reload::defaults::well_capture_below_m,
		        .motion = physical_reload::action_motion::charging_handle,
		        .receiver_release = action.receiver_release};
	}
	// Measured M4 reference contact, also explicitly rebased by M16. The
	// receiver-specific travel, rest pose and release paddle remain local.
	inline constexpr hands::anchor contact_reference{{-.80446699f, 0, 4.50075705f}, {0, 0, 0, 1}};
	inline constexpr hands::vec contact_low{-1.1f, -1.3f, 3.8f}, contact_high{.4f, 1.3f, 4.9f};
}
