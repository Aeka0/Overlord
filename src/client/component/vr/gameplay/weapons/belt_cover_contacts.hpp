#pragma once
#include "../belt_profile.hpp"

namespace vr::gameplay::weapons::belt_cover_contacts
{
	// Capture rectangles based on LOD0 cover skin, with acceptance margins,
	// in closed hinge coordinates (metres). See
	// docs/vr-belt-fed-weapons.md. Extend 4 cm toward the hinge and 1 cm
	// at the other three edges. Exterior approach still gates acquisition.
	inline constexpr belt_feed::cover_push_profile rpd{.025f,.265f,-.055f,.055f,.006f};
	inline constexpr belt_feed::cover_push_profile m240{.040f,.290f,-.058f,.060f,.022f};
	inline constexpr belt_feed::cover_push_profile mg4{.045f,.315f,-.058f,.050f,.019f};
}
