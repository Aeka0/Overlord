#pragma once
#include "../../physical_reload_gesture.hpp"
#include "../../pistol_magazine_capture.hpp"

namespace vr::gameplay::weapons::m9
{
	inline constexpr float slide_return_seconds = .075f;
	// Interaction tuning candidate, in metres, separate from native ammo rules.
	// Exported slide travel is along local -X; manual-pull clip peaks near 6 cm
	// and last-fire lock near 4.7 cm. Contact tolerances need HMD validation.
	inline constexpr physical_reload::profile reload_interaction{
		.18f, part_grip_capture::radius_m, .061f, .047f, .055f,
		.18f, pistol_magazine_capture::radius_m, pistol_magazine_capture::inside_m,
		pistol_magazine_capture::insertion_cosine, .25f, {-1, 0, 0},
		pistol_magazine_capture::below_m, .35f, .04f, .003f, 2};
	// Extend the original waist spheres UP only, retaining standing reach.
	inline constexpr float waist_half_width_m = .21f, waist_down_m = .62f, waist_extend_up_m = .20f;
	inline constexpr float magazine_exit_seconds = .16f, magazine_exit_clearance_m = .01f;
}
