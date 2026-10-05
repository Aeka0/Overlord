#pragma once
#include "actions.hpp"
#include "poses.hpp"
#include "reload_profile.hpp"

namespace vr::gameplay::weapons::m9
{
	inline constexpr profile base{
		"m9",  "wpn_h1_pst_m9_vm", "base",	   aim_rule::rear_hand, wrists, 1, 0.10f, 0.22f,
		0.10f, idle_fingers,	   equip_rest, suppress_equip, &physical, {part_visibility::rigid_groups}};
}
