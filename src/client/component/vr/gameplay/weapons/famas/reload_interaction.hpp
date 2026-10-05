#pragma once
#include "../../physical_reload_configuration.hpp"
#include "../../part_grip_pose.hpp"
#include "../../magazine_manipulation.hpp"

namespace vr::gameplay::weapons::famas
{
	// j_reload_trigger sits ahead of the magazine; strike it toward the stock.
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
	    .grab_radius = physical_reload::defaults::magazine_grab_radius_m,
	    .pull_travel = physical_reload::defaults::magazine_pull_travel_m,
	    .pull_lateral_limit = physical_reload::defaults::magazine_pull_lateral_limit_m,
	    .pull_axis = {0, 0, -1},
	    .latch_radius = physical_reload::defaults::magazine_latch_radius_m,
	    .latch_rearm_radius = physical_reload::defaults::magazine_latch_rearm_radius_m,
	    .latch_min_speed = physical_reload::defaults::magazine_latch_min_speed_mps,
	    .latch_min_travel = physical_reload::defaults::magazine_latch_min_travel_m,
	    .latch_direction = {-1, 0, 0},
	    .spare_strike = true,
	};
	// Native reload/fire moves j_bolt 82.55 mm. The retained empty pose is an
	// authored presentation of the requested follower lock, not empty_additive.
	inline constexpr physical_reload::profile reload_interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = part_grip_capture::radius_m,
	    .slide_stroke = .083f,
	    .locked_travel = .075f,
	    .full_stroke = .078f,
	    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	    .well_radius = physical_reload::defaults::well_radius_m,
	    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
	    .insertion_cosine = physical_reload::defaults::insertion_cosine,
	    .max_contact_step = physical_reload::defaults::max_contact_step_m,
	    .slide_axis = physical_reload::defaults::rearward_axis,
	    .well_capture_below = physical_reload::defaults::well_capture_below_m,
	    .motion = physical_reload::action_motion::reciprocating_slide,
	    .manual_magazine = &manual_magazine,
	};
}
