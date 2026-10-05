#pragma once
#include "../../families/ar.hpp"

namespace vr::gameplay::weapons::m4
{
	// Same closed-bolt transaction authority as the pistols. These data alone
	// DO NOT admit a native weapon: full assembly/render contracts are required.
	inline constexpr auto reload_rules=families::ar::reload_rules;
	inline constexpr auto chamber_rules=mechanics::chamber_rules(reload_rules);
}
