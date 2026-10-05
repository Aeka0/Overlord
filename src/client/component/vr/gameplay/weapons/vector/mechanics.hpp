#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::vector
{
	inline constexpr mechanics::rules reload_rules{30,mechanics::magazine_release::button,true,true,true};
}
