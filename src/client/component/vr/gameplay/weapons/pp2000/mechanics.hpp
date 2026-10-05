#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::pp2000
{
	// Button magazine release, chamber plus-one, no automatic last-round lock
	// and no bolt-release control. An empty reload must finish with a full rack.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 20,
	    .release = mechanics::magazine_release::button,
	    .last_round_lock = false,
	    .release_control = false,
	    .plus_one = true,
	};
}
