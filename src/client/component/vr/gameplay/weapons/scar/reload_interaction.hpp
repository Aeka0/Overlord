#pragma once
#include "../../physical_reload_gesture.hpp"
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::scar
{
	// j_reload reciprocates in fire and stays 138.77 mm rearward in empty_additive.
	// Six millimetres of manual overtravel distinguish pulling off the follower
	// lock from merely grabbing the already-open handle.
	// Receiver SHA-256 9062f1d89ff16e6a57afb0c77c86bd097b27a0d81a0a8956894f35816afa78cf.
	// Reviewed upper paddle on the left receiver mesh (no separate bone), cm / 2.54.
	inline constexpr physical_reload::receiver_bolt_release bolt_release{{3.66350196f,.91177746f,3.63592144f}};
	inline constexpr physical_reload::profile reload_interaction=[] {
		physical_reload::profile p{
		.18f,part_grip_capture::radius_m,action_stroke_m+.006f,action_stroke_m,action_stroke_m+.003f,
		.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,2,
		physical_reload::action_motion::reciprocating_slide};
		p.receiver_release=&bolt_release;return p;
	}();
}
