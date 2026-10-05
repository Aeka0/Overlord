#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"

namespace vr::gameplay::weapons::m1911
{
	inline constexpr profile base{
		"m1911","h2_viewmodel_colt45_base","base",aim_rule::rear_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
}
