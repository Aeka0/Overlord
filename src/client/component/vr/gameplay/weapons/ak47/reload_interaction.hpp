#pragma once
#include "../../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::ak47
{
	// Metres. Pull follows the initial native forward/downward exit. A new
	// magazine approaches the rear latch toward +X; proximity alone cannot eject.
	inline constexpr auto manual_magazine=physical_reload::rocking_magazine({.35f,0,-.93674970f});
	// j_bolt2 travels 9.35 cm on pullout_first. It reciprocates on firing and
	// always returns forward after release, including over an empty magazine.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.094f,0.f,.087f,.18f,.045f,.06f,.08715574f,.25f,
		{-.999791f,0,-.020444f},.06f,.35f,.04f,.003f,2,
		physical_reload::action_motion::reciprocating_slide,&manual_magazine};
}
