#pragma once
#include "../../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::vector
{
	// Native reload_empty frames 59..61: 60.025mm axial pull after unfolding.
	// The handle returns forward/folded even while the internal bolt is locked.
	// Centre the complete exposed left j_switch paddle, export cm / 2.54.
	// The former target was on its foremost tip and missed the far end.
	// SHA256 b7801e00d2aec40edb6f9e5ef7bc2a9e4d263ab149167cc8e5c0a2b9321ece7b.
	inline constexpr physical_reload::receiver_bolt_release bolt_release{.centre={7.42558607f,.89196201f,1.09648545f},.visual_bone="j_switch"};
	inline constexpr physical_reload::profile reload_interaction=[] {
		physical_reload::profile p{
		.18f,part_grip_capture::radius_m,.061f,0.f,.055f,
		.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,1,
		physical_reload::action_motion::charging_handle};
		p.receiver_release=&bolt_release;return p;
	}();
}
