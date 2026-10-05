#include <std_include.hpp>
#include "gulag.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"
#include "component/scheduler.hpp"
#include "../native_scripted_control.hpp"
#include "../native_carry.hpp"
#include "../native_ammunition.hpp"
#include <mutex>

namespace vr::gameplay::sequences::gulag
{
	namespace
	{
		// Classification belongs to the entity lifetime. Permission remains live:
		// VM-derived sequence ownership, native life/link state and weapon gates.
		std::mutex use_mutex;
		std::array<use_witness, 4000> use_targets{};
		void publish_use_kind(int entity, use_kind kind) noexcept
		{
			const use_witness value{weapons::native_ammunition::timeline(),
			                        weapons::native_carry::entity_key(entity).generation,
			                        kind};
			const std::lock_guard lock(use_mutex);
			use_targets[entity] = value;
		}
	}
	bool supported()
	{
		const auto* map = game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string) == "gulag";
	}
	view observe()
	{
		const scripting::entity actor{game::scr_entref_t{0, 0}};
		if (!scripting::call<int>("isalive", {actor}))
			return {};
		if (!actor.call("islinked").as<int>())
		{
			const scripting::entity level{*game::levelEntityId};
			const auto value = level.get("flag");
			if (!value.is<scripting::array>())
				return {};
			const auto flags = value.as<scripting::array>();
			const auto set = [&](const char* key)
			{
				const auto v = flags.get(std::string(key));
				return v.is<int>() && v.as<int>() != 0;
			};
			// Live ready frame has weapon=0, weapon flags=0x80 and native cursor
			// entity 1766 (player_uses_rig). Use permission is not weapon permission.
			return rope_use({.alive = true,
			                 .evacuation_begun = set("evac_begins"),
			                 .linked = false,
			                 .used = set("player_uses_rig")});
		}
		const auto parent = actor.call("getlinkedparent");
		// _id_B123 initially links to the legacy camera (0xCC0B), but the
		// remastered _id_CCE4 then links to animated player_rappel (0xC438).
		// Live capture: parent object 26799/entity 1460 was 0xC438, whereas
		// 0xCC0B was object 27230/entity 499. _id_D313 unlinks/deletes the rig.
		// Compare identities, never the shared player_rappel animation name.
		const auto controller = scripting::get_object_variable(*game::levelEntityId, 0xCC0Bu);
		const auto animated = scripting::get_object_variable(*game::levelEntityId, 0xC438u);
		const auto id = [](const scripting::script_value& v)
		{ return v.is<scripting::entity>() ? v.as<scripting::entity>().get_entity_id() : 0u; };
		const auto camera_tag = intro_tag(id(parent), id(controller), id(animated));
		if (camera_tag != game_view::scripted_camera_tag::none)
		{
			return classify({.alive = true, .linked = true, .intro_controller = true, .tag = camera_tag});
		}
		if (!parent.is<scripting::entity>())
			return {};
		const auto body = parent.as<scripting::entity>();
		const auto rig = body.get("animname");
		if (!rig.is<std::string>())
			return {};
		const auto name = rig.as<std::string>();
		const scripting::entity level{*game::levelEntityId};
		if (name == "player_rig")
		{
			// _id_C18E can first attach to its hidden temporary player_rig,
			// then hand off to level.player_rig. Both belong to one accepted rope
			// attachment; do not confuse the earlier rock-removal animation with it.
			const auto flags = level.get("flag");
			if (flags.is<scripting::array>())
			{
				const auto values = flags.as<scripting::array>();
				const auto set = [&](const char* key)
				{
					const auto v = values.get(std::string(key));
					return v.is<int>() && v.as<int>() != 0;
				};
				const auto escape = evacuation({.alive = true,
				                                .linked = true,
				                                .rig = name,
				                                .begun = set("evac_begins"),
				                                .used = set("player_uses_rig")});
				if (escape.stage != phase::none)
					return escape;
			}
			// _id_B331 hands the breach victim to this rig; _id_BF06 later
			// tightens view clamps repeatedly. Live parent 2333/object35375
			// was linked below price_breach_ent 2140/object35371.
			const auto anchor = body.call("getlinkedparent");
			return cinematic({.alive = true,
			                  .linked = true,
			                  .rig = name,
			                  .price_anchor = same_entity(id(anchor), id(level.get("price_breach_ent"))),
			                  .cafeteria = false,
			                  .evacuation_rig = same_entity(id(parent), id(level.get("player_rig")))});
		}
		if (name == "worldbody")
		{
			const auto value = level.get("flag");
			if (!value.is<scripting::array>())
				return {};
			const auto cafeteria = value.as<scripting::array>().get(std::string("do_cafeteria_anims"));
			// _id_D551 owns the falling-rock player_downed clip. The transient
			// player_falls_down flag clears before cleanup, so retain the camera
			// for the actual linked body instead of ending at that flag edge.
			return cinematic({.alive = true,
			                  .linked = true,
			                  .rig = name,
			                  .price_anchor = false,
			                  .cafeteria = cafeteria.is<int>() && cafeteria.as<int>() != 0,
			                  .evacuation_rig = false});
		}
		return {};
	}
	bool allows_use(int entity) noexcept
	{
		if (!supported())
			return true;
		if (!scheduler::is_executing(scheduler::pipeline::server) || !*game::levelEntityId || entity <= 0 ||
		    entity >= 4000)
			return false;
		try
		{
			const scripting::entity target{game::scr_entref_t{static_cast<unsigned short>(entity), 0}};
			const auto flag = target.get("script_flag"), rig = target.get("animname");
			const auto flag_name = flag.is<std::string>() ? flag.as<std::string>() : "";
			// The native map still contains the old extraction trigger beside
			// player_uses_rig. _id_AD05 deletes its hookup_rope_ent and the H2
			// ending uses _id_C18E instead of maps/gulag_code::_id_BE66.
			// Live pre-evac capture found both triggers within two metres. The
			// old one must never become a second VR interaction after readiness.
			if (retired_rope_target(flag_name))
			{
				publish_use_kind(entity, use_kind::retired);
				return false;
			}
			if (!rope_target(flag_name, rig.is<std::string>() ? rig.as<std::string>() : ""))
			{
				publish_use_kind(entity, use_kind::ordinary);
				return scripted_control::allowed(game::g_entities[0].client);
			}
			publish_use_kind(entity, use_kind::rope);
			const scripting::entity level{*game::levelEntityId}, actor{game::scr_entref_t{0, 0}};
			const auto value = level.get("flag");
			if (!value.is<scripting::array>())
				return false;
			const auto flags = value.as<scripting::array>();
			const auto set = [&](const char* name)
			{
				const auto v = flags.get(std::string(name));
				return v.is<int>() && v.as<int>() != 0;
			};
			// maps/gulag_ending::_id_C18E sets evac_begins while linked to the
			// authored flare/rope animation. Only after its player-rig "end" does
			// it unlink, start GULAG_SPIE_HINT and wait for player_uses_rig.
			// makeusable on ending_rope1 happens earlier and cannot authorize VR
			// hover/activation. Do not latch an early squeeze across this boundary.
			return rope_ready({.alive = scripting::call<int>("isalive", {actor}) != 0,
			                   .evacuation_begun = set("evac_begins"),
			                   .linked = actor.call("islinked").as<int>() != 0,
			                   .used = set("player_uses_rig")});
		}
		catch (const std::exception&)
		{
			publish_use_kind(entity, use_kind::unknown);
			return false;
		}
	}
	bool allows_native_use(int entity, std::uint64_t generation) noexcept
	{
		if (!supported())
			return true;
		if (entity <= 0 || entity >= int(use_targets.size()))
			return false;
		use_witness witness;
		{
			const std::lock_guard lock(use_mutex);
			witness = use_targets[entity];
		}
		const auto kind = witness.current(weapons::native_ammunition::timeline(), generation);
		const auto* ps = game::g_entities[0].client;
		bool dead{};
		std::uintptr_t link{};
		if (!player_life::read(ps, dead) ||
		    !utils::native_memory::read_bytes(
		        &link, reinterpret_cast<const std::byte*>(&game::g_entities[0]) + 0x208, sizeof(link)))
			return false;
		return native_use_allowed(
		    kind,
		    sequences::for_player(ps),
		    {.weapon_permission = scripted_control::allowed(ps), .alive = !dead, .linked = link != 0});
	}
}
