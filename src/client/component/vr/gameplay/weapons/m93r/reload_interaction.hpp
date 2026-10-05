#pragma once
#include "../../physical_reload_gesture.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::m93r
{
	// M93R last-fire settles at 4.609cm; reload-empty holds at 4.616cm.
	// Manual range is a candidate based on its 7.392cm peak slide excursion.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.075f,.04616412f,.067f,
		.18f,pistol_magazine_capture::radius_m,pistol_magazine_capture::inside_m,
		pistol_magazine_capture::insertion_cosine,.25f,{-1,0,0},
		pistol_magazine_capture::below_m,.35f,.04f,.003f,1};
}
