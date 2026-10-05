#include <std_include.hpp>
#include "breach.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"
namespace vr::gameplay::sequences::breach
{
	phase observe()
	{
		const scripting::entity player{game::scr_entref_t{0, 0}}, level{*game::levelEntityId};
		if (!scripting::call<int>("isalive", {player}) || !player.call("islinked").as<int>())
			return phase::none;
		const auto parent = player.call("getlinkedparent");
		if (!parent.is<scripting::entity>())
			return phase::none;
		const auto rig = parent.as<scripting::entity>().get("animname");
		if (!rig.is<std::string>())
			return phase::none;
		const auto slowmo = level.get("breaching");
		// Native slowmo_begins sets this persistent field; transient notetrack
		// flags are consumed and cleared by another script thread in the same tick.
		return classify(true, true, rig.as<std::string>(), slowmo.is<int>() && slowmo.as<int>() != 0);
	}
}
