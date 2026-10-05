#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::ump
{
	inline constexpr mechanics::rules reload_rules = []
	{
		mechanics::rules r{
		    .magazine_capacity = 25,
		    .release = mechanics::magazine_release::physical_pull,
		    .last_round_lock = true,
		    .release_control = false,
		    .plus_one = true,
		    .feed = mechanics::feed_type::closed_bolt,
		    .manual_catch = true,
		};
		r.physical_catch_release = true;
		return r;
	}();
}
