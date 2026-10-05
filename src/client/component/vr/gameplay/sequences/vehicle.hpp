#pragma once
#include "../scripted_sequences.hpp"
#include "../vehicle_policy.hpp"

namespace vr::gameplay::sequences::vehicle
{
	inline view waterfall(bool jumping,bool linked,bool boat_parent,bool blend_parent) noexcept
	{
		if(!jumping || !linked || (!boat_parent && !blend_parent))return {};
		auto result=free_look(scenario::vehicle,phase::execution);result.camera=scene_cameras::vehicle_exit;
		result.suspend_weapons=true;result.allow_movement=result.allow_turn=false;return result;
	}
	view observe();
}
