#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::de50
{
	// Base single-wield candidate. Admission MUST also verify live native name,
	// base/effective capacity and matching complete viewmodel; no size-only opt-in.
	inline constexpr const char* native_name = "deserteagle";
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 7,
	    .release = mechanics::magazine_release::button,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
	inline constexpr auto chamber_rules = mechanics::chamber_rules(reload_rules);
}
