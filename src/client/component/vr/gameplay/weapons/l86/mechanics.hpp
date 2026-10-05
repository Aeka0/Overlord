#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::l86
{
	// H2 drum capacity candidate; native admission still checks the base capacity.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 100,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
}
