#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::l86
{
	// H2 drum capacity candidate; native admission still checks the base capacity.
	inline constexpr mechanics::rules reload_rules{100,mechanics::magazine_release::physical_pull,true,true,true};
}
