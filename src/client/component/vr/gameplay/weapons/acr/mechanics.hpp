#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::acr
{
	// M4-style closed-bolt feed, follower lock, button release and tactical +1.
	inline constexpr mechanics::rules reload_rules{30,mechanics::magazine_release::button,true,true,true};
}
