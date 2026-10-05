#pragma once
#include "actions.hpp"
#include "poses.hpp"
#include "reload_profile.hpp"

namespace vr::gameplay::weapons::m9
{
	inline constexpr profile base{
	    .id = "m9",
	    .receiver = "wpn_h1_pst_m9_vm",
	    .variant = "base",
	    .aiming = aim_rule::rear_hand,
	    .wrists = wrists,
	    .authored_rear = 1,
	    .acquire_meters = 0.10f,
	    .release_meters = 0.22f,
	    .blend_seconds = 0.10f,
	    .fingers = idle_fingers,
	    .equip_rest = equip_rest,
	    .suppress_equip = suppress_equip,
	    .reload = &physical,
	    .viewmodel =
	        {
	            .visibility = part_visibility::rigid_groups,
	        },
	};
}
