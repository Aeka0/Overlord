#pragma once
#include "../../physical_reload_gesture.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::de50
{
	// Metres. Exported lock is 76.34 mm; retain a small manual
	// overtravel beyond lock for a deliberate full stroke. HMD tuning pending.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.087f,.07634f,.082f,
		.18f,pistol_magazine_capture::radius_m,pistol_magazine_capture::inside_m,
		pistol_magazine_capture::insertion_cosine,.25f,{-1,0,0},
		pistol_magazine_capture::below_m,.35f,.04f,.003f,1};
}
