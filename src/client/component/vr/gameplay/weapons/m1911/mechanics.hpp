#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::m1911
{
	// Base single-wield candidate. Admission MUST also verify live native name,
	// base/effective capacity and matching complete viewmodel; no size-only opt-in.
	inline constexpr const char* native_name = "colt45";
	inline constexpr mechanics::rules reload_rules{7,mechanics::magazine_release::button,true,true,true};
	inline constexpr auto chamber_rules = mechanics::chamber_rules(reload_rules);
}

