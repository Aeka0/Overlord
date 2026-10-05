#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::miniuzi
{
	// Exact single-wield native candidate. Cocking never extracts or feeds a
	// persistent chamber round; the last accepted shot closes the action.
	inline constexpr const char* native_name = "uzi";
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 32,
	    .release = mechanics::magazine_release::button,
	    .last_round_lock = false,
	    .release_control = false,
	    .plus_one = false,
	    .feed = mechanics::feed_type::open_bolt,
	};
}
