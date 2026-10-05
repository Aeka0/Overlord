#pragma once
#include "../../physical_reload_configuration.hpp"
#include "../../part_grip_pose.hpp"
#include "../../magazine_manipulation.hpp"
#include "../../handle_catch.hpp"

namespace vr::gameplay::weapons::mp5
{
	// Preserve the downward pull and reuse the shared forward latch strike.
	inline constexpr auto manual_magazine = physical_reload::rocking_magazine();
	inline constexpr physical_reload::handle_catch manual_catch{};
	// Native rear stroke; the shared catch handles lift, roll and palm impact.
	inline constexpr physical_reload::profile reload_interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = part_grip_capture::radius_m,
	    .slide_stroke = 0.065f,
	    .locked_travel = 0.f,
	    .full_stroke = 0.060f,
	    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	    .well_radius = physical_reload::defaults::well_radius_m,
	    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
	    .insertion_cosine = physical_reload::defaults::insertion_cosine,
	    .max_contact_step = physical_reload::defaults::max_contact_step_m,
	    .slide_axis = physical_reload::defaults::rearward_axis,
	    .well_capture_below = physical_reload::defaults::well_capture_below_m,
	    .slide_pose_count = 2,
	    .motion = physical_reload::action_motion::charging_handle,
	    .manual_magazine = &manual_magazine,
	    .manual_catch = &manual_catch,
	};
}
