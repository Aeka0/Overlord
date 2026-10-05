#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::acr
{
	// M4-style closed-bolt feed, follower lock, button release and tactical +1.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 30,
	    .release = mechanics::magazine_release::button,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
}
