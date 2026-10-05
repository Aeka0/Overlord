#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"

namespace vr::gameplay::weapons::de50
{
	inline constexpr profile base{
		"de50","h2_viewmodel_desert_eagle_base","base",aim_rule::rear_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical};
	// Gold receiver has the identical 11-bone bind; keep its own material identity.
	inline constexpr profile gold=[] {auto p=base;p.receiver="h2_viewmodel_desert_eagle_gold";return p;}();
}

