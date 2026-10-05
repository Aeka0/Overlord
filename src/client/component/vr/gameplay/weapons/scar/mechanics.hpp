#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::scar
{
	// Host rifle only: detachable twenty-round magazine, follower lock and +1.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 20,
	    .release = mechanics::magazine_release::button,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
}
