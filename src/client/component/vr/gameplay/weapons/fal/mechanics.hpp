#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::fal
{
	// Physical magazine latch; the control releases only an empty follower lock.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 20,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
}
