#pragma once
#include "../../physical_reload_configuration.hpp"
#include "../../part_grip_pose.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::m9
{
	inline constexpr float slide_return_seconds = .075f;
	// Interaction tuning candidate, in metres, separate from native ammo rules.
	// Exported slide travel is along local -X; manual-pull clip peaks near 6 cm
	// and last-fire lock near 4.7 cm. Contact tolerances need HMD validation.
	inline constexpr physical_reload::profile reload_interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = part_grip_capture::radius_m,
	    .slide_stroke = .061f,
	    .locked_travel = .047f,
	    .full_stroke = .055f,
	    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	    .well_radius = pistol_magazine_capture::radius_m,
	    .well_contact_depth = pistol_magazine_capture::inside_m,
	    .insertion_cosine = pistol_magazine_capture::insertion_cosine,
	    .max_contact_step = physical_reload::defaults::max_contact_step_m,
	    .slide_axis = physical_reload::defaults::rearward_axis,
	    .well_capture_below = pistol_magazine_capture::below_m,
	    .slide_pose_count = 2,
	};
	// Extend the original waist spheres UP only, retaining standing reach.
	inline constexpr float waist_half_width_m = .21f, waist_down_m = .62f, waist_extend_up_m = .20f;
	inline constexpr float magazine_exit_seconds = .16f, magazine_exit_clearance_m = .01f;
}
