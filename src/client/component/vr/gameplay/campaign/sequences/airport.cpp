#include <std_include.hpp>
#include "airport.hpp"
#include "airport_opening.hpp"
#include "../../script_function_extent.hpp"
#include "../../native_weapon_read.hpp"
#include "../../official_cheats.hpp"
#include "../../../head_pose_bridge.hpp"
#include "component/scripting.hpp"
#include "component/notifies.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::airport
{
	namespace opening
	{
		namespace
		{
			std::atomic_bool retained{};
			std::array<const char*, 10> hooks{};
			constexpr auto saved_field = "vr_airport_retained_m240";
			bool skip_replacement()
			{
				return retained.load();
			}
			bool enter()
			{
				const auto* enabled = game::Dvar_FindVar("vr_enable");
				const auto* hands = game::Dvar_FindVar("vr_independentHands");
				if (!enabled || !enabled->current.enabled || !hands || !hands->current.enabled ||
				    !head_pose_bridge::get_status().enabled || cheats::green_beret())
					return false;
				const auto weapon = weapons::native_weapon_read::find("m240");
				const auto* ps = game::g_entities[0].client;
				bool owned{};
				for (unsigned i = 0; weapon && ps && i < 15; ++i)
				{
					unsigned token{};
					if (!utils::native_memory::read_at(ps, 0x2f8 + i * 4, token))
						return false;
					owned |= token == weapon;
				}
				if (!owned)
					return false;
				try
				{
					const scripting::entity level{*game::levelEntityId};
					level.set(saved_field, 1);
					retained = true;
				}
				catch (const std::exception& e)
				{
					console::error("[VR airport] opening entry rejected: %s\n", e.what());
				}
				return false; // Observe entry; execute the original opcode.
			}
			bool finish()
			{
				if (retained)
					try
					{
						const scripting::entity level{*game::levelEntityId};
						level.set(saved_field, 0);
						retained = false;
					}
					catch (const std::exception& e)
					{
						console::error("[VR airport] opening exit rejected: %s\n", e.what());
					}
				return false;
			}
			void clear()
			{
				retained = false;
				for (auto*& site : hooks)
					if (site)
					{
						notifies::clear_hook(site);
						site = nullptr;
					}
			}
			void bind()
			{
				clear();
				if (!supported())
					return;
				try
				{
					const auto* begin =
					    scripting::get_function_pos("maps/airport_code", scripting::get_token_single(0xc8e1));
					std::optional<std::pair<std::uintptr_t, std::uintptr_t>> extent;
					for (const auto& [file, entries] : scripting::script_function_table_sort)
						if ((extent =
						         script_function_extent(entries, reinterpret_cast<std::uintptr_t>(begin))))
							break;
					if (!extent)
						return;
					const scripting::script_value m240{"m240"}, prop{"saw_airport"};
					const auto sites =
					    inspect({reinterpret_cast<const std::byte*>(begin), extent->second - extent->first},
					            m240.get_raw().u.stringValue,
					            prop.get_raw().u.stringValue);
					if (!sites)
					{
						console::error("[VR airport] opening script contract rejected\n");
						return;
					}
					hooks[0] = begin;
					notifies::set_gsc_hook(begin, begin, enter);
					for (std::size_t i = 0; i < sites->replacements.size(); ++i)
					{
						const auto* site = begin + sites->replacements[i];
						hooks[i + 1] = site;
						notifies::set_gsc_hook(site, begin + sites->resume(i), skip_replacement);
					}
					hooks.back() = begin + sites->finish;
					notifies::set_gsc_hook(hooks.back(), hooks.back(), finish);
					// Checkpoint continuations can resume after entry. Native
					// saved state, not an elapsed-time estimate, restores the gate.
					const scripting::entity level{*game::levelEntityId};
					const auto saved = level.get(saved_field);
					retained = saved.is<int>() && saved.as<int>() == 1;
				}
				catch (const std::exception& e)
				{
					clear();
					console::error("[VR airport] opening binding rejected: %s\n", e.what());
				}
			}
		}
		bool active() noexcept
		{
			return retained.load();
		}
		class component final : public component_interface
		{
			void post_unpack() override
			{
				scripting::on_level_start(bind);
				scripting::on_shutdown(
				    [](bool free_scripts, bool after)
				    {
					    if (!after)
					    {
						    if (free_scripts)
							    clear();
						    else
							    retained = false;
					    }
				    });
			}
		};
	}
	bool supported()
	{
		const auto* map = game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string) == "airport";
	}
	view observe()
	{
		const scripting::entity level{*game::levelEntityId}, actor{game::scr_entref_t{0, 0}};
		const auto value = level.get("flag");
		if (!value.is<scripting::array>())
			return {};
		const auto flags = value.as<scripting::array>();
		const auto flag = [&](const char* key)
		{
			const auto v = flags.get(std::string(key));
			return v.is<int>() && v.as<int>() != 0;
		};
		evidence e{flag("escape_player_get_in"), flag("escape_player_shot")};
		if ((!e.boarding && !e.shot) || !actor.call("islinked").as<int>())
			return {};
		const auto parent = actor.call("getlinkedparent");
		// airport_code::_id_B41A (boarding) and _id_CE05 (alternate shot)
		// both store the linked player_ending body in level._id_B499.
		// The fire notetrack calls airport::_id_B70F, which raises
		// escape_player_shot. Do not infer that boundary from health or time.
		const auto body = scripting::get_object_variable(level.get_entity_id(), 0xB499u);
		if (!parent.is<scripting::entity>() || !body.is<scripting::entity>())
			return {};
		e.parent = parent.as<scripting::entity>().get_entity_id();
		e.body = body.as<scripting::entity>().get_entity_id();
		if (e.parent != e.body)
			return {};
		const auto rig = parent.as<scripting::entity>().get("animname");
		const auto name = rig.is<std::string>() ? rig.as<std::string>() : std::string{};
		e.rig = name;
		return classify(e);
	}
}
REGISTER_COMPONENT(vr::gameplay::sequences::airport::opening::component)
