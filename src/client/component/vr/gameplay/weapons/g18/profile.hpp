#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"

namespace vr::gameplay::weapons::g18
{
	inline constexpr profile base{
		"g18","h2_viewmodel_glock_base","base",aim_rule::rear_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
}
