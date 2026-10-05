#pragma once
#include "../scripted_sequences.hpp"
#include <string_view>

namespace vr::gameplay::sequences::favela
{
	inline view classify(bool alive, bool linked, std::string_view rig) noexcept
	{
		view result{};
		if (!alive || !linked || rig != "player_rig")
			return result;
		result.scene = scenario::favela;
		result.stage = phase::transport;
		result.camera = scene_cameras::favela;
		// Camera ownership only. Native linking/weapon permission still owns
		// movement and combat; the existing stick-to-stance binding must keep
		// reaching this scene's notifyoncommand("go_crouch", ...) listener.
		result.allow_movement = result.allow_turn = true;
		return result;
	}
	bool supported();
	view observe();
}
