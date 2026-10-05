#pragma once
#include "../scripted_sequences.hpp"
#include <string_view>

namespace vr::gameplay::sequences::estate
{
	inline view classify(bool ending,bool linked,std::string_view rig,bool weapons_allowed) noexcept
	{
		view result{};
		if(!ending || !linked || rig!="worldbody")return result;
		result.scene=scenario::estate;
		// Native enable/disable owns this boundary. Stow/draw and slot exchange
		// may temporarily empty selection without starting Shepherd's animation.
		const bool combat=weapons_allowed;
		result.stage=combat?phase::scripted_combat:phase::execution;
		result.suspend_weapons=!combat;
		result.retain_weapon=result.hide_body_arms=combat;
		result.camera=combat?scene_cameras::estate_drag:scene_cameras::estate_ending;
		if(!combat)result.rotation_tag=game_view::scripted_camera_tag::player;
		result.allow_turn=combat;
		return result;
	}
	bool supported();
	view observe(const void* player_state);
}
