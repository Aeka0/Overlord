#pragma once

namespace vr::gameplay::weapons::pistol_magazine_capture
{
	// Shared semi-auto pistol accessibility tuning, not magazine geometry or
	// a rifle/revolver default. Well-local +Z runs from the mouth into the grip.
	// HMD refinement: extend only the upper/inside reach from 6 to 9 cm.
	// Lower reach, 7 cm diameter, seated pose and ejection rail stay unchanged.
	// This same volume drives bare-hand/knife insertion and debug geometry.
	inline constexpr float radius_m = .035f, below_m = .06f, inside_m = .09f;
	// cos(95 degrees): another 10 degrees of wrist-angle slack, including a
	// small overshoot past perpendicular. Reversed magazines remain rejected.
	inline constexpr float insertion_cosine = -.08715574f;
}
