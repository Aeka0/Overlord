#include <std_include.hpp>
#include "roadkill.hpp"
#include "../../mounted_turret_policy.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::roadkill
{
	bool supported()
	{
		const auto* map = game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string) == "roadkill";
	}
	view observe(const scripting::entity& parent, unsigned entity_flags)
	{
		if (mounted::attached(entity_flags))
			return {};
		const auto rig = parent.get("animname");
		if (!rig.is<std::string>())
			return {};
		const scripting::entity level{*game::levelEntityId};
		const auto value = level.get("flag");
		if (!value.is<scripting::array>())
			return {};
		if (rig.as<std::string>() == "player_worldbody")
		{
			// _id_B5F9 links the player to player_shep_intro after the RPG
			// intro, then unlinks/deletes this body before get_on_the_line.
			// Do not match the initial h2_intro or the later exit_latvee body.
			const auto flags = value.as<scripting::array>();
			const auto intro = flags.get(std::string("h2_intro_done")),
			           finished = flags.get(std::string("get_on_the_line"));
			if (!intro.is<int>() || !finished.is<int>())
				return {};
			return classify_recovery(
			    {true, intro.as<int>() != 0, finished.as<int>() != 0, false, "player_worldbody"});
		}
		if (rig.as<std::string>() != "player_rig" || !parent.call("islinked").as<int>())
			return {};
		const auto triggered = value.as<scripting::array>().get(std::string("player_gets_in"));
		// _id_A9A3 stores this vehicle, links its temporary player_rig to
		// tag_body, then links the player to that rig for player_getin.
		// _id_AA05 replaces the player parent and calls turret useby afterwards.
		// Compare actual identities; a broad player_rig match also covers
		// unrelated cinematics, and player_gets_in remains set after boarding.
		const auto vehicle = scripting::get_object_variable(*game::levelEntityId, 0xBA6Bu);
		const auto anchor = parent.call("getlinkedparent");
		const auto id = [](const scripting::script_value& v)
		{ return v.is<scripting::entity>() ? v.as<scripting::entity>().get_entity_id() : 0u; };
		return classify({true,
		                 triggered.is<int>() && triggered.as<int>() != 0,
		                 false,
		                 "player_rig",
		                 id(anchor),
		                 id(vehicle)});
	}
}
