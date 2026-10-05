#pragma once
#include <array>
#include "weapon_hud_channels.hpp"
namespace vr::gameplay::weapon_hud
{
	struct profile
	{
		float width_meters{.26f};
		// Forward/left/up relative to rear grip, calibrated from the M9's old
		// muzzle-minus-10-cm placement. Barrel length no longer affects the HUD.
		std::array<float,3> from_grip_meters{.17f,.02f,.11f};
		float screen_side_meters{.12f};
		float screen_up_meters{.04f};
	};
	inline constexpr profile generic{};
	inline constexpr profile for_feed(profile layout,feed channel) noexcept
	{
		// User-selected compact vertical offset below the primary row.
		if (channel==feed::underbarrel) layout.screen_up_meters-=.05f;
		return layout;
	}
	inline float screen_side(const profile& layout,bool left) noexcept
	{ return left ? -layout.screen_side_meters : layout.screen_side_meters; }
}
