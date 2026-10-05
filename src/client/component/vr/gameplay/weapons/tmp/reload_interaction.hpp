#pragma once
#include "../../physical_reload_gesture.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::tmp
{
	// Native pullout/reload-empty handle peaks at 8.729cm. It always returns
	// forward independently of the internal bolt and never follows shot recoil.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.088f,0.f,.079f,
		.18f,pistol_magazine_capture::radius_m,pistol_magazine_capture::inside_m,
		pistol_magazine_capture::insertion_cosine,.25f,{-1,0,0},
		pistol_magazine_capture::below_m,.35f,.04f,.003f,1,
		physical_reload::action_motion::charging_handle};
}
