#pragma once

#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::m9
{
	// Runtime admission must verify the native base M9 capacity. Never patch the
	// shared WeaponDef to make room for +1: chamber is an instance-owned state.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 15,
	    .release = mechanics::magazine_release::button,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
	inline constexpr auto chamber_rules = mechanics::chamber_rules(reload_rules);
	// Native asset name, independent of a level's weapon index or render cache.
	inline constexpr const char* native_name = "beretta";
} // namespace vr::gameplay::weapons::m9
