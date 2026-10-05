#pragma once
#include "../../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::famas
{
	// j_reload_trigger sits ahead of the magazine; strike it toward the stock.
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
		.05f,.05f,.10f,{0,0,-1},.025f,.06f,.15f,.012f,{-1,0,0},true};
	// Native reload/fire moves j_bolt 82.55 mm. The retained empty pose is an
	// authored presentation of the requested follower lock, not empty_additive.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.083f,.075f,.078f,.18f,.045f,.06f,.08715574f,.25f,
		{-1,0,0},.06f,.35f,.04f,.003f,1,
		physical_reload::action_motion::reciprocating_slide,&manual_magazine};
}
