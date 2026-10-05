#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::usp
{
	// Native desktop capture 2026-09-08: weapon23 "usp", capacity12, no dual,
	// segmented or additive reload. Assembly is also required for physical mode.
	// Do not admit tactical/akimbo/silenced variants by a name prefix.
	inline constexpr const char* native_name = "usp";
	// Live campaign capture: token16, same 12-round closed-bolt mechanism.
	inline constexpr const char* silenced_native_name = "usp_silencer";
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 12,
	    .release = mechanics::magazine_release::button,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
	inline constexpr auto chamber_rules = mechanics::chamber_rules(reload_rules);
}
