#pragma once
#include "../../physical_reload_gesture.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::usp
{
	// Metres. H2 settled lastfire locks at 50.45 mm (H1 is 50.45 mm too).
	// Manual 60 mm travel / 56 mm full threshold adds deliberate overtravel.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.060f,.05045f,.056f,
		.18f,pistol_magazine_capture::radius_m,pistol_magazine_capture::inside_m,
		pistol_magazine_capture::insertion_cosine,.25f,{-1,0,0},
		pistol_magazine_capture::below_m,.35f,.04f,.003f,1};
}
