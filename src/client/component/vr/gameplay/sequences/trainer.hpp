#pragma once
#include "../scripted_sequences.hpp"

namespace vr::gameplay::sequences::trainer
{
	inline view classify(bool initialized,
	                     bool training_finished,
	                     bool linked,
	                     bool weapons_allowed,
	                     std::string_view parent_class,
	                     std::string_view rig) noexcept
	{
		if (!initialized || training_finished || !linked || weapons_allowed ||
		    (parent_class != "script_origin" && rig != "worldbody"))
			return {};
		view result{};
		result.scene = scenario::trainer;
		result.stage = phase::execution;
		// Both pickups first interpolate a script_origin, then link to the
		// animated worldbody. Preserve one head heading across that handoff.
		result.camera = scene_cameras::trainer;
		result.suspend_weapons = true;
		return result;
	}
	bool supported();
	view observe(const void* player_state);
}
