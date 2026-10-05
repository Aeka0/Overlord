#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::pp2000
{
	// Button magazine release, chamber plus-one, no automatic last-round lock
	// and no bolt-release control. An empty reload must finish with a full rack.
	inline constexpr mechanics::rules reload_rules{20,mechanics::magazine_release::button,false,false,true};
}
