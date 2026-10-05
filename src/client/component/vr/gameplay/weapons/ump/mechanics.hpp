#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::ump
{
	inline constexpr mechanics::rules reload_rules=[] {
		mechanics::rules r{25,mechanics::magazine_release::physical_pull,true,false,true,mechanics::feed_type::closed_bolt,true};
		r.physical_catch_release=true;return r;
	}();
}
