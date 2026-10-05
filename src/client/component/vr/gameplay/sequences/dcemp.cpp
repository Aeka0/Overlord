#include <std_include.hpp>
#include "dcemp.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::dcemp
{
	bool supported()
	{
		const auto* map = game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string) == "dcemp";
	}
	view observe(const scripting::entity& actor, const scripting::entity& parent)
	{
		const auto rig = parent.get("animname");
		const auto name = rig.is<std::string>() ? rig.as<std::string>() : std::string{};
		// dcemp_code::_id_B7A4 binds the ISS camera to its dedicated iss_rig.
		// dc_crashsite stores the waking body in player._id_C309. Match that
		// object as well as player_rig: dcemp later reuses the name for rescue.
		evidence observed{name, parent.get_entity_id()};
		if (name == "iss_rig")
			return classify(observed);
		if (name == "player_rig")
		{
			const auto body = scripting::get_object_variable(actor.get_entity_id(), 0xC309u);
			if (body.is<scripting::entity>())
				observed.wakeup = body.as<scripting::entity>().get_entity_id();
			return classify(observed);
		}
		// _id_C3CB links the same movement carrier twice, 0.35 s apart, then
		// restores native angles and raises emp_back_from_whiteout. Wait for
		// that boundary; an earlier reset would be overwritten by the script.
		const auto ground = scripting::get_object_variable(actor.get_entity_id(), 0xB0D8u);
		if (!ground.is<scripting::entity>() ||
		    ground.as<scripting::entity>().get_entity_id() != observed.parent)
			return {};
		observed.ground = observed.parent;
		const scripting::entity level{*game::levelEntityId};
		const auto flags = level.get("flag");
		if (!flags.is<scripting::array>())
			return {};
		const auto returned = flags.as<scripting::array>().get(std::string("emp_back_from_whiteout"));
		observed.returned = returned.is<int>() && returned.as<int>() != 0;
		return classify(observed);
	}
}
