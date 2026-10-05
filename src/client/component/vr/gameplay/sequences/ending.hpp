#pragma once
#include "../scripted_sequences.hpp"
#include "../ending_interaction.hpp"

namespace vr::gameplay::sequences::ending
{
	inline view presentation(::vr::gameplay::ending::stage stage) noexcept
	{
		namespace story = ::vr::gameplay::ending;
		if (stage == story::stage::none)
			return {};
		auto result = free_look(scenario::ending,
		                        stage == story::stage::wakeup     ? phase::recovery
		                        : stage == story::stage::approach ? phase::scripted_combat
		                                                          : phase::execution);
		result.camera = scene_cameras::ending_body;
		result.rotation_tag = game_view::scripted_camera_tag::player;
		if (stage == story::stage::approach)
		{
			result.camera = scene_cameras::ending_approach;
			result.rotation_tag = game_view::scripted_camera_tag::none;
		}
		else if (stage == story::stage::subdual)
			result.camera = scene_cameras::ending_subdual;
		else if (stage == story::stage::wakeup)
			result.camera = scene_cameras::ending_wakeup;
		else if (stage == story::stage::wounded)
			result.camera = scene_cameras::ending_wounded;
		result.suspend_weapons = stage != story::stage::approach;
		result.allow_movement = result.allow_turn = stage == story::stage::approach;
		result.retain_weapon = result.block_carry = true;
		result.hide_chest_equipment = true;
		return result;
	}
	bool supported();
	view observe();
}
