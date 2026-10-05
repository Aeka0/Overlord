#pragma once
#include "../usp/poses.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::designator
{
	// Reuse USP fingers and support grip, translated by the measured trigger
	// offset: native designator (2.204810,0,2.241040) minus USP source cm/2.54.
	inline constexpr auto wrists = []
	{
		auto value = usp::wrists;
		for (auto& w : value)
		{
			w.position[0] += .204988f;
			w.position[2] += .542963f;
		}
		return value;
	}();
	inline bool suppress_equip(std::string_view name) noexcept
	{
		return base_equip_action(name, "h2_wpn_pst_laserdesignator_");
	}
	inline constexpr profile base{
	    .id = "laserdesignator",
	    .receiver = "h2_viewmodel_laser_designator_base",
	    .variant = "mission_device",
	    .aiming = aim_rule::rear_hand,
	    .wrists = wrists,
	    .authored_rear = 1,
	    .acquire_meters = profile_defaults::acquire_meters,
	    .release_meters = profile_defaults::release_meters,
	    .blend_seconds = profile_defaults::blend_seconds,
	    .fingers = usp::idle_fingers,
	    .equip_rest = {},
	    .suppress_equip = suppress_equip,
	};
}
