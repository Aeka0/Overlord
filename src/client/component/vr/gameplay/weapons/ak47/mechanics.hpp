#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::ak47
{
	// User-authored scope: no controller release and no last-round bolt hold.
	// A chambered round survives a tactical magazine change; empty needs a rack.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 30,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = false,
	    .release_control = false,
	    .plus_one = true,
	};
}
