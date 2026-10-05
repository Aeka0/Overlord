#pragma once
#include "../../physical_reload_gesture.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::g18
{
	// Exported settled last_fire: 46.018 mm. Native first-pull peak 50.655 mm.
	// Manual 55 mm / full 51 mm adds a small deliberate stroke past the lock.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.055f,.046018f,.051f,.18f,
		pistol_magazine_capture::radius_m,pistol_magazine_capture::inside_m,
		pistol_magazine_capture::insertion_cosine,.25f,{-1,0,0},
		pistol_magazine_capture::below_m,.35f,.04f,.003f,1};
}
