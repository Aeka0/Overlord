#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::fal
{
	// Physical magazine latch; the control releases only an empty follower lock.
	inline constexpr mechanics::rules reload_rules{20,mechanics::magazine_release::physical_pull,true,true,true};
}
