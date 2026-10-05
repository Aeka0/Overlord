#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"

namespace vr::gameplay::weapons::g18
{
	inline constexpr profile base{
	    .id = "g18",
	    .receiver = "h2_viewmodel_glock_base",
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
	    .viewmodel =
	        {
	            .visibility = part_visibility::rigid_groups,
	        },
	};
}
