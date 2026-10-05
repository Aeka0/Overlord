#pragma once
#include "../../physical_reload_configuration.hpp"
#include "../../part_grip_pose.hpp"
#include "../../magazine_manipulation.hpp"

namespace vr::gameplay::weapons::ak47
{
	// Metres. Pull follows the initial native forward/downward exit. A new
	// magazine approaches the rear latch toward +X; proximity alone cannot eject.
	inline constexpr auto manual_magazine = physical_reload::rocking_magazine({.35f, 0, -.93674970f});
	// j_bolt2 travels 9.35 cm on pullout_first. It reciprocates on firing and
	// always returns forward after release, including over an empty magazine.
	inline constexpr physical_reload::profile reload_interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = part_grip_capture::radius_m,
	    .slide_stroke = .094f,
	    .locked_travel = 0.f,
	    .full_stroke = .087f,
	    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	    .well_radius = physical_reload::defaults::well_radius_m,
	    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
	    .insertion_cosine = physical_reload::defaults::insertion_cosine,
	    .max_contact_step = physical_reload::defaults::max_contact_step_m,
	    .slide_axis = {-.999791f, 0, -.020444f},
	    .well_capture_below = physical_reload::defaults::well_capture_below_m,
	    .slide_pose_count = 2,
	    .motion = physical_reload::action_motion::reciprocating_slide,
	    .manual_magazine = &manual_magazine,
	};
}
