#pragma once
#include "../../weapon_hud_profile.hpp"
namespace vr::gameplay::weapons::m9
{
	// Whole native widget extent, including transparent art padding. Placement
	// is a first HMD tuning candidate, not a gun-rotating textured surface.
	// Shared whole-centimeter rear-grip placement, including the left-hand offset.
	inline constexpr weapon_hud::profile hud=weapon_hud::generic;
}
