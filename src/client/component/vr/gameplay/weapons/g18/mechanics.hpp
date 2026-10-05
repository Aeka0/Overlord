#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::g18
{
	// Single-wield candidate; exact native name and base capacity are checked
	// together with the complete receiver. Native firing cadence stays native.
	inline constexpr const char* native_name = "glock";
	// Live H2 WeaponDef base clip size is 32; the chamber permits 32+1.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 32,
	    .release = mechanics::magazine_release::button,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
	inline constexpr auto chamber_rules = mechanics::chamber_rules(reload_rules);
}
