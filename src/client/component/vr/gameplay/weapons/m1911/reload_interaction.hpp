#pragma once
#include "../../physical_reload_gesture.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::m1911
{
	// Metres. Exported lock is 45.17 mm; retain a small manual
	// overtravel beyond lock for a deliberate full stroke. HMD tuning pending.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.055f,.04517f,.050f,
		.18f,pistol_magazine_capture::radius_m,pistol_magazine_capture::inside_m,
		pistol_magazine_capture::insertion_cosine,.25f,{-1,0,0},
		pistol_magazine_capture::below_m,.35f,.04f,.003f,1};
}
