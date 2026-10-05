#pragma once
#include "../../physical_reload_gesture.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::miniuzi
{
	// Upper j_bolt is the non-reciprocating handle. Native pullout_first peaks
	// at 84.634mm; j_open_reload separately represents the internal bolt.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.085f,0.f,.077f,
		.18f,pistol_magazine_capture::radius_m,pistol_magazine_capture::inside_m,
		pistol_magazine_capture::insertion_cosine,.25f,{-1,0,0},
		pistol_magazine_capture::below_m,.35f,.04f,.003f,1,
		physical_reload::action_motion::charging_handle};
}
