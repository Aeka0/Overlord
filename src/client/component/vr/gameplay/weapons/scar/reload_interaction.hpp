#pragma once
#include "../../physical_reload_configuration.hpp"
#include "../../part_grip_pose.hpp"
#include "../../receiver_bolt_release.hpp"
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::scar
{
	// j_reload reciprocates in fire and stays 138.77 mm rearward in empty_additive.
	// Six millimetres of manual overtravel distinguish pulling off the follower
	// lock from merely grabbing the already-open handle.
	// Receiver SHA-256 9062f1d89ff16e6a57afb0c77c86bd097b27a0d81a0a8956894f35816afa78cf.
	// Reviewed upper paddle on the left receiver mesh (no separate bone), cm / 2.54.
	inline constexpr physical_reload::receiver_bolt_release bolt_release{
	    {3.66350196f, .91177746f, 3.63592144f}};
	inline constexpr physical_reload::profile reload_interaction = []
	{
		physical_reload::profile p{
		    .waist_radius = physical_reload::defaults::waist_radius_m,
		    .slide_radius = part_grip_capture::radius_m,
		    .slide_stroke = action_stroke_m + .006f,
		    .locked_travel = action_stroke_m,
		    .full_stroke = action_stroke_m + .003f,
		    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
		    .well_radius = physical_reload::defaults::well_radius_m,
		    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
		    .insertion_cosine = physical_reload::defaults::insertion_cosine,
		    .max_contact_step = physical_reload::defaults::max_contact_step_m,
		    .slide_axis = physical_reload::defaults::rearward_axis,
		    .well_capture_below = physical_reload::defaults::well_capture_below_m,
		    .slide_pose_count = 2,
		    .motion = physical_reload::action_motion::reciprocating_slide,
		};
		p.receiver_release = &bolt_release;
		return p;
	}();
}
