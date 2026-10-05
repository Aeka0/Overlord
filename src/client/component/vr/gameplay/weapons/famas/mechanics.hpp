#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::famas
{
	inline constexpr mechanics::rules reload_rules{30,mechanics::magazine_release::physical_pull,true,true,true};
}
