#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"

namespace vr::gameplay::weapons::m93r
{
	inline constexpr profile base{
		"m93r","h2_viewmodel_beretta_393_base","foregrip",aim_rule::rear_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
}
