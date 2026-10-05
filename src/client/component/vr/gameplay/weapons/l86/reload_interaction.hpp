#pragma once
#include "../../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::l86
{
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
		.05f,.05f,.10f,{0,0,-1},.025f,.06f,.15f,.012f,{1,0,0},false};
	// The audited 798-triangle j_reload group contains both the bolt face and
	// handle. Stock fire is static; VR presents the whole carrier reciprocating.
	// Native manual travel is 85.46 mm. The 78 mm follower stop is authored
	// inside the existing 86 mm stroke, not sampled from stock empty animation.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.086f,.078f,.080f,.18f,.045f,.06f,.08715574f,.25f,
		{-1,0,0},.06f,.35f,.04f,.003f,2,
		physical_reload::action_motion::reciprocating_slide,&manual_magazine};
}
