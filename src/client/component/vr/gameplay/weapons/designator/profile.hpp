#pragma once
#include "../usp/poses.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::designator
{
	// Reuse USP fingers and support grip, translated by the measured trigger
	// offset: native designator (2.204810,0,2.241040) minus USP source cm/2.54.
	inline constexpr auto wrists=[] {auto value=usp::wrists;for(auto& w:value){w.position[0]+=.204988f;w.position[2]+=.542963f;}return value;}();
	inline bool suppress_equip(std::string_view name)noexcept{return base_equip_action(name,"h2_wpn_pst_laserdesignator_");}
	inline constexpr profile base{"laserdesignator","h2_viewmodel_laser_designator_base","mission_device",aim_rule::rear_hand,
		wrists,1,.10f,.22f,.10f,usp::idle_fingers,{},suppress_equip};
}
