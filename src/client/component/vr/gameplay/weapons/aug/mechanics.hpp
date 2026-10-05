#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::aug
{
	inline constexpr mechanics::rules reload_rules{30,mechanics::magazine_release::physical_pull,false,false,true,mechanics::feed_type::closed_bolt,true};
}
