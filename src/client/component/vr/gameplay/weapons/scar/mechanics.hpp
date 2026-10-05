#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::scar
{
	// Host rifle only: detachable twenty-round magazine, follower lock and +1.
	inline constexpr mechanics::rules reload_rules{20,mechanics::magazine_release::button,true,true,true};
}
