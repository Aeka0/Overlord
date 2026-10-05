#include <std_include.hpp>
#include "oilrig.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"
#include "component/scheduler.hpp"

namespace vr::gameplay::sequences::oilrig
{
	namespace
	{
		bool flag(const scripting::array& flags, const char* key)
		{
			const auto value = flags.get(std::string{key});
			return value.is<int>() && value.as<int>() != 0;
		}
	}
	bool supported()
	{
		const auto* map = game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string) == "oilrig";
	}
	bool underwater_entry()
	{
		if (!*game::levelEntityId || !supported())
			return false;
		const scripting::entity level{*game::levelEntityId};
		const auto value = level.get("flag");
		if (!value.is<scripting::array>())
			return false;
		const auto flags = value.as<scripting::array>();
		return flag(flags, "underwater_sequence_lighting") && !flag(flags, "player_breaks_surface") &&
		       !flag(flags, "player_is_done_swimming") && !flag(flags, "player_starting_stealth_kill") &&
		       !flag(flags, "player_done_being_helped_from_water");
	}
	equipment current_equipment() noexcept
	{
		if (!supported())
			return equipment::unrestricted;
		if (!scheduler::is_executing(scheduler::pipeline::server) || !*game::levelEntityId)
			return equipment::unavailable;
		try
		{
			const auto value = scripting::entity{*game::levelEntityId}.get("flag");
			if (!value.is<scripting::array>())
				return equipment::unavailable;
			const auto detonated = value.as<scripting::array>().get(std::string{"ambush_c4_triggered"});
			if (!detonated.is<int>())
				return equipment::unavailable;
			return detonated.as<int>() ? equipment::claymore : equipment::detonator;
		}
		catch (const std::exception&)
		{
			return equipment::unavailable;
		}
	}
	view observe_evacuation(const scripting::entity& parent)
	{
		const scripting::entity actor{game::scr_entref_t{0, 0}};
		const auto body = actor.get("worldbody_rig");
		if (!body.is<scripting::entity>())
			return {};
		const auto rig = body.as<scripting::entity>();
		const auto animation = rig.get("animname");
		if (!animation.is<std::string>() || animation.as<std::string>() != "worldbody")
			return {};
		const auto value = scripting::entity{*game::levelEntityId}.get("flag");
		if (!value.is<scripting::array>())
			return {};
		const auto flags = value.as<scripting::array>();
		const auto type = parent.get("classname");
		std::uint32_t weapon_flags{};
		if (const auto* client = game::g_entities[0].client)
			std::memcpy(&weapon_flags, reinterpret_cast<const std::byte*>(client) + 0x3c0, 4);
		// _id_CC8C first links to a temporary script_origin for 0.7 seconds,
		// then to this exact worldbody_rig. Native disable/enable and the later
		// takeallweapons -> M14 exchange remain authoritative throughout.
		const bool helper =
		    type.is<std::string>() && type.as<std::string>() == "script_origin" && (weapon_flags & 0x80) != 0;
		return evacuation({.alive = scripting::call<int>("isalive", {actor}) != 0,
		                   .parent = parent.get_entity_id(),
		                   .worldbody = rig.get_entity_id(),
		                   .boarded = flag(flags, "player_on_board_littlebird"),
		                   .landed = flag(flags, "escape_littlebird_landed"),
		                   .boarding_helper = helper});
	}
	phase observe()
	{
		const scripting::entity level{*game::levelEntityId};
		const scripting::entity actor{game::scr_entref_t{0, 0}};
		const auto value = level.get("flag");
		if (!value.is<scripting::array>())
			return phase::none;
		const auto flags = value.as<scripting::array>();
		const bool linked = actor.call("islinked").as<int>() != 0;
		const bool swim_done = flag(flags, "player_is_done_swimming"),
		           kill_started = flag(flags, "player_starting_stealth_kill");
		std::uint32_t weapon_flags{};
		if (const auto* client = game::g_entities[0].client)
			std::memcpy(&weapon_flags, reinterpret_cast<const std::byte*>(client) + 0x3c0, 4);
		const bool recovery =
		    kill_started && !linked && flag(flags, "player_turn_rate_slowed") && (weapon_flags & 0x80) != 0;
		bool kill_rig{};
		if (linked && swim_done && !kill_started)
		{
			// playerlinktoblend takes ownership half a second BEFORE the kill
			// flag. The surface controller's tag_origin has no player_rig animname.
			const auto parent = actor.call("getlinkedparent");
			if (parent.is<scripting::entity>())
			{
				const auto animation = parent.as<scripting::entity>().get("animname");
				kill_rig = animation.is<std::string>() && animation.as<std::string>() == "player_rig";
			}
		}
		// The surface checkpoint deliberately skips player_attached_to_sdv.
		// The native looking flag includes the near-guard and 25-degree checks.
		return classify({scripting::call<int>("isalive", {actor}) != 0,
		                 linked,
		                 flag(flags, "player_attached_to_sdv"),
		                 flag(flags, "player_breaks_surface"),
		                 swim_done,
		                 flag(flags, "player_looking_at_grate_guard"),
		                 kill_started,
		                 flag(flags, "player_ready_to_be_helped_from_water"),
		                 flag(flags, "player_done_being_helped_from_water"),
		                 flag(flags, "obj_stealthkill_complete"),
		                 kill_rig,
		                 recovery});
	}
}
