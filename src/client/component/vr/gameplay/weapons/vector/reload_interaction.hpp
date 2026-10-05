#pragma once
#include "../../physical_reload_configuration.hpp"
#include "../../part_grip_pose.hpp"
#include "../../receiver_bolt_release.hpp"

namespace vr::gameplay::weapons::vector
{
	// Native reload_empty frames 59..61: 60.025mm axial pull after unfolding.
	// The handle returns forward/folded even while the internal bolt is locked.
	// Centre the complete exposed left j_switch paddle, export cm / 2.54.
	// The former target was on its foremost tip and missed the far end.
	// SHA256 b7801e00d2aec40edb6f9e5ef7bc2a9e4d263ab149167cc8e5c0a2b9321ece7b.
	inline constexpr physical_reload::receiver_bolt_release bolt_release{
	    .centre = {7.42558607f, .89196201f, 1.09648545f}, .visual_bone = "j_switch"};
	inline constexpr physical_reload::profile reload_interaction = []
	{
		physical_reload::profile p{
		    .waist_radius = physical_reload::defaults::waist_radius_m,
		    .slide_radius = part_grip_capture::radius_m,
		    .slide_stroke = .061f,
		    .locked_travel = 0.f,
		    .full_stroke = .055f,
		    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
		    .well_radius = physical_reload::defaults::well_radius_m,
		    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
		    .insertion_cosine = physical_reload::defaults::insertion_cosine,
		    .max_contact_step = physical_reload::defaults::max_contact_step_m,
		    .slide_axis = physical_reload::defaults::rearward_axis,
		    .well_capture_below = physical_reload::defaults::well_capture_below_m,
		    .motion = physical_reload::action_motion::charging_handle,
		};
		p.receiver_release = &bolt_release;
		return p;
	}();
}
