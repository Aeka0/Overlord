#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::mp5
{
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 30,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = false,
	    .release_control = false,
	    .plus_one = true,
	    .feed = mechanics::feed_type::closed_bolt,
	    .manual_catch = true,
	};
}
