#pragma once
#include "../scripted_sequences.hpp"
#include "../cliffhanger/level_rules.hpp"

namespace scripting
{
	class entity;
}

namespace vr::gameplay::sequences::cliffhanger
{
	inline view classify(std::string_view start,
	                     bool flags_ready,
	                     bool reached_top,
	                     bool alive,
	                     unsigned parent,
	                     std::string_view body = {},
	                     bool ledge_started = false,
	                     bool native_transition = false,
	                     bool rescue = false) noexcept
	{
		if (!alive || !flags_ready || reached_top || !::vr::gameplay::cliffhanger::opening_start(start) ||
		    !parent || parent >= 4000)
			return {};
		auto result = free_look(scenario::cliffhanger);
		// Helpers without an authored camera tag retain the current heading.
		result.camera = scene_cameras::cliffhanger_helper;
		// A camera helper is not necessarily the animated body. Only the actual
		// linked worldbody may reserve hands; the renderer also checks its model.
		if (body == "worldbody")
		{
			// Native yaw is sampled from the unmodified camera bone, not the
			// clamped player angles. Tracked pitch/roll and relative yaw stay free.
			result.camera = rescue ? scene_cameras::cliffhanger_rescue : scene_cameras::cliffhanger_body;
			result.rotation_tag = game_view::scripted_camera_tag::player;
			result.arms = {scripted_arms::model_profile::arctic,
			               !native_transition &&
			                       (ledge_started || start == "climb" || start == "jump" || start == "e3")
			                   ? scripted_arms::control::tracked
			                   : scripted_arms::control::authored,
			               int(parent),
			               3};
		}
		// Native get-up input and body/root travel remain authoritative.
		return result;
	}
	bool supported();
	view observe(const scripting::entity& parent);
}
