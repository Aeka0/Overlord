#pragma once
#include "../../physical_reload_configuration.hpp"
#include "../../part_grip_pose.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::miniuzi
{
	// Upper j_bolt is the non-reciprocating handle. Native pullout_first peaks
	// at 84.634mm; j_open_reload separately represents the internal bolt.
	inline constexpr physical_reload::profile reload_interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = part_grip_capture::radius_m,
	    .slide_stroke = .085f,
	    .locked_travel = 0.f,
	    .full_stroke = .077f,
	    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	    .well_radius = pistol_magazine_capture::radius_m,
	    .well_contact_depth = pistol_magazine_capture::inside_m,
	    .insertion_cosine = pistol_magazine_capture::insertion_cosine,
	    .max_contact_step = physical_reload::defaults::max_contact_step_m,
	    .slide_axis = physical_reload::defaults::rearward_axis,
	    .well_capture_below = pistol_magazine_capture::below_m,
	    .motion = physical_reload::action_motion::charging_handle,
	};
}
