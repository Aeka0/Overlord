#pragma once
#include "../../physical_reload_gesture.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::pp2000
{
	// Native first_time_pullout j_reload peak is 64.19 mm. Fire also translates
	// this root; retain native recoil. Its front tip stays in the straight pose.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.065f,0.f,.058f,
		.18f,pistol_magazine_capture::radius_m,pistol_magazine_capture::inside_m,
		pistol_magazine_capture::insertion_cosine,.25f,{-1,0,0},
		pistol_magazine_capture::below_m,.35f,.04f,.003f,1,
		physical_reload::action_motion::reciprocating_slide};
}
