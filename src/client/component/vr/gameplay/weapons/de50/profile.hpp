#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"

namespace vr::gameplay::weapons::de50
{
	inline constexpr profile base{
	    .id = "de50",
	    .receiver = "h2_viewmodel_desert_eagle_base",
	    .variant = "base",
	    .aiming = aim_rule::rear_hand,
	    .wrists = wrists,
	    .authored_rear = 1,
	    .acquire_meters = profile_defaults::acquire_meters,
	    .release_meters = profile_defaults::release_meters,
	    .blend_seconds = profile_defaults::blend_seconds,
	    .fingers = idle_fingers,
	    .equip_rest = equip_rest,
	    .suppress_equip = suppress_equip,
	    .reload = &physical,
	};
	// Gold receiver has the identical 11-bone bind; keep its own material identity.
	inline constexpr profile gold = []
	{
		auto p = base;
		p.receiver = "h2_viewmodel_desert_eagle_gold";
		return p;
	}();
}
