#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::m93r
{
	// Exact single-wield candidate; native name/base capacity and receiver must
	// all agree. The engine retains burst timing and accepted-shot accounting.
	inline constexpr const char* native_name="beretta393";
	inline constexpr mechanics::rules reload_rules{20,mechanics::magazine_release::button,true,true,true};
	inline constexpr auto chamber_rules=mechanics::chamber_rules(reload_rules);
}
