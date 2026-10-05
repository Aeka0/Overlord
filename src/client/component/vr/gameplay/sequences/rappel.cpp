#include <std_include.hpp>
#include "rappel.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::rappel
{
	bool supported()
	{
		const auto* map=game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string)=="af_caves";
	}
	phase observe(bool was_active)
	{
		const scripting::entity level{*game::levelEntityId};
		const scripting::entity actor{game::scr_entref_t{0,0}};
		const auto value=level.get("flag");
		if (!value.is<scripting::array>()) return phase::none;
		const auto flags=value.as<scripting::array>();
		const auto flag=[&](const char* key) {
			const auto f=flags.get(std::string{key});return f.is<int>() && f.as<int>()!=0;
		};
		// Native weapon enable precedes melee opportunity; end_of_rappel_scene
		// precedes unlink. Neither event alone releases the animated body.
		const bool linked=actor.call("islinked").as<int>()!=0;
		const bool hooked=flag("player_hooking_up") || (linked &&
			level.get("player_rig").is<scripting::entity>() && actor.call("hasweapon",{"rappel_knife"}).as<int>()!=0);
		return classify({true,scripting::call<int>("isalive",{actor})!=0,
			linked,hooked,flag("descending"),
			flag("rappel_end"),flag("player_killing_guard"),flag("end_of_rappel_scene"),
			flag("player_failed_rappel"),was_active});
	}
}
