#include <std_include.hpp>
#include "favela.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::favela
{
	bool supported()
	{
		const auto* map=game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string)=="favela";
	}
	view observe()
	{
		const scripting::entity actor{game::scr_entref_t{0,0}};
		if (!scripting::call<int>("isalive",{actor}) || !actor.call("islinked").as<int>()) return {};
		const auto parent=actor.call("getlinkedparent");
		if (!parent.is<scripting::entity>()) return {};
		const auto rig=parent.as<scripting::entity>().get("animname");
		// maps/favela's opening passenger is its only player_rig. The script
		// sets start_chase before getout and clears the exit flag mid-animation;
		// neither flag ends camera ownership. Native unlink after getout does.
		return rig.is<std::string>() ? classify(true,true,rig.as<std::string>()) : view{};
	}
}
