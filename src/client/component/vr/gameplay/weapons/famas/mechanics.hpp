#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::famas
{
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 30,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
}
