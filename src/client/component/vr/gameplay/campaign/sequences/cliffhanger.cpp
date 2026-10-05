#include <std_include.hpp>
#include "cliffhanger.hpp"
#include "../cliffhanger/physical.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::cliffhanger
{
	bool supported()
	{
		const auto* map = game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string) == "cliffhanger";
	}
	view observe(const scripting::entity& parent)
	{
		// Called only after the common observer has confirmed a living player
		// and its actual current parent. Do not depend on the equipment
		// provider, selected ice-picker definition, animation time or weapon
		// permission: the native opening replaces all of those along the way.
		const scripting::entity level{*game::levelEntityId};
		const auto start = level.get("start_point"), value = level.get("flag");
		if (!start.is<std::string>() || !value.is<scripting::array>())
			return {};
		const auto reached = value.as<scripting::array>().get(std::string("reached_top"));
		const auto ledge = value.as<scripting::array>().get(std::string("ledge_started"));
		const auto transition = value.as<scripting::array>().get(std::string("player_preps_for_jump"));
		const auto hanging = value.as<scripting::array>().get(std::string("player_hangs_on"));
		const auto final = value.as<scripting::array>().get(std::string("final_climb"));
		// Native flags span the hanging/catch animation after the big jump;
		// final_climb hands ownership back to the second climbing entry.
		const bool rescue = hanging.is<int>() && hanging.as<int>() && final.is<int>() && !final.as<int>();
		const auto body = parent.get("animname");
		auto result = classify(start.as<std::string>(),
		                       reached.is<int>(),
		                       reached.is<int>() && reached.as<int>() != 0,
		                       true,
		                       parent.get_entity_reference().entnum,
		                       body.is<std::string>() ? body.as<std::string>() : "",
		                       ledge.is<int>() && ledge.as<int>() != 0,
		                       transition.is<int>() && transition.as<int>() != 0,
		                       rescue);
		if (::vr::gameplay::cliffhanger_physical::preserve_native_arms(result.arms.entity))
			result.arms.mode = scripted_arms::control::authored;
		if (::vr::gameplay::cliffhanger_physical::independent_hands())
		{
			result.arms = {};
			result.camera = scene_cameras::cliffhanger_physical;
			result.rotation_tag = game_view::scripted_camera_tag::none;
			result.allow_movement = result.allow_turn = false;
		}
		return result;
	}
}
