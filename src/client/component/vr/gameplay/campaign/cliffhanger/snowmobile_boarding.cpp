#include <std_include.hpp>
#include "snowmobile_boarding.hpp"
#include "../../native_use.hpp"
#include "../../native_carry.hpp"
#include "../../../hud_prompts.hpp"
#include "../../../head_pose_bridge.hpp"
#include "../../../controller_input.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::cliffhanger::boarding
{
	namespace
	{
		gate boarding;
		bool reported_error{};
		bool enabled()
		{
			const auto* map = game::Dvar_FindVar("mapname");
			const auto* vr = game::Dvar_FindVar("vr_enable");
			const auto* world = game::Dvar_FindVar("vr_worldInteraction");
			return map && map->current.string && std::string_view(map->current.string) == "cliffhanger" &&
			       vr && vr->current.enabled && world && world->current.enabled &&
			       interaction::native::ready() && game::CL_IsCgameInitialized() && game::SV_Loaded();
		}
		hands::vec vector(const scripting::script_value& v)
		{
			const auto p = v.as<scripting::vector>();
			return {p[0], p[1], p[2]};
		}
		void update()
		{
			using clock = controller_input::clock;
			if (!enabled() || !*game::levelEntityId || *game::g_script_error_level != -1)
			{
				boarding.interrupt();
				return;
			}
			const auto now = clock::now();
			const auto fresh = [&](clock::time_point at) { return now >= at && now - at <= 150ms; };
			const auto input = controller_input::latest_interaction();
			const auto head = head_pose_bridge::get_status();
			const auto* paused = game::Dvar_FindVar("cl_paused");
			head_pose_bridge::spatial_frame body;
			if (!paused || paused->current.integer || *game::keyCatchers || !head.enabled ||
			    !head.pose_available || head.recenter_pending || !input.focused || input.orientation_settling ||
			    !input.sequence || !fresh(input.sampled_at) || !head_pose_bridge::get_spatial_frame(body) ||
			    !fresh(body.captured_at) || body.generation != input.reference_generation)
			{
				boarding.interrupt();
				return;
			}
			const scripting::entity level{*game::levelEntityId}, player{game::scr_entref_t{0, 0}};
			const auto flags_value = level.get("flag");
			if (!flags_value.is<scripting::array>()) { boarding.interrupt(); return; }
			const auto flags = flags_value.as<scripting::array>();
			const auto flag = [&](const char* name, int expected)
			{
				const auto v = flags.get(std::string(name));
				// Availability must be explicitly set. Completion flags may not
				// exist yet on a native checkpoint before their trigger initializes.
				return v.is<int>() ? v.as<int>() == expected : expected == 0 && v.get_raw().type == game::SCRIPT_NONE;
			};
			// maps/cliffhanger_snowmobile::_id_D300 waits for stop_lerp and
			// another 0.75 s, then publishes this exact vehicle, starts AF69/C525
			// and raises availability. Neither spawn nor zero velocity suffices.
			if (!flag("player_snowmobile_available", 1) || !flag("player_rides_snowmobile", 0) ||
			    !flag("player_starts_snowmobile_trip", 0) || !scripting::call<int>("isalive", {player}) ||
			    player.call("islinked").as<int>() || player.get("vehicle").is<scripting::entity>())
			{
				boarding.interrupt();
				return;
			}
			const auto value = scripting::get_object_variable(level.get_entity_id(), 0xBE57u);
			if (!value.is<scripting::entity>()) { boarding.interrupt(); return; }
			const auto vehicle = value.as<scripting::entity>();
			const auto ref = vehicle.get_entity_reference();
			if (ref.classnum || !ref.entnum || ref.entnum >= 4000) { boarding.interrupt(); return; }
			const auto identity = weapons::native_carry::entity_key(ref.entnum);
			// C525 creates its script_origin at tag_driver + (0,0,30), only on
			// the story boarding branch. Direct snowmobile/ending starts use a
			// separate native vehicle_mount path and have no such use target.
			auto point = vector(vehicle.call("gettagorigin", {"tag_driver"}));
			point[2] += 30.f;
			const auto delta = hands::sub(point, body.head_position);
			const auto distance = hands::length(delta);
			if (!std::isfinite(distance) || distance < .01f * body.units_per_meter ||
			    distance > reach_meters * body.units_per_meter)
			{
				boarding.interrupt();
				return;
			}
			// Reuse native usable-target admission and visibility. Aim this
			// proximity query at the authored point; no hand ray or button is
			// required, and no other nearby interaction can receive the event.
			const interaction::ray aim{body.head_position, hands::scale(delta, 1.f / distance),
			                           body.head_position, body.units_per_meter, reach_meters, 1.f};
			const auto target = interaction::native::query(aim);
			if (!target || target.weapon || !interaction::native::live(target) ||
			    hud_prompts::world_message(target.hint) != game_text::key::snowmobile_board ||
			    hands::length(hands::sub(target.position, point)) > 1.f)
			{
				boarding.interrupt();
				return;
			}
			const scripting::entity trigger{game::scr_entref_t{static_cast<unsigned short>(target.key.entity), 0}};
			if (trigger.get("classname").as<std::string>() != "script_origin")
			{
				boarding.interrupt();
				return;
			}
			if (!boarding.update({true, false, false, true, {identity.entity, identity.generation}, target.key,
			                      vector(vehicle.get("origin")), vector(vehicle.call("vehicle_getvelocity")),
			                      distance, body.units_per_meter, input.reference_generation,
			                      input.continuity_generation}, now))
				return;
			// Resume the existing trigger waiter. It deletes the proxy, plays
			// cliffhanger_code/C25A and calls useby itself; never set mission flags
			// or mount a vehicle directly. No script pointer survives a load.
			scripting::notify(trigger, "trigger", {player});
			boarding.consumed(target.key);
			console::info("[VR snowmobile] Auto boarding vehicle=%u trigger=%d\n", ref.entnum, target.key.entity);
		}
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			scripting::on_level_start([] { boarding.reset(); reported_error = false; });
			scripting::on_shutdown([](bool, bool after) { if (!after) boarding.reset(); });
			scheduler::loop([]
			{
				try { update(); }
				catch (const std::exception& e)
				{
					boarding.interrupt();
					if (!reported_error) console::error("[VR snowmobile] Boarding rejected: %s\n", e.what());
					reported_error = true;
				}
			}, scheduler::pipeline::server, 50ms);
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::cliffhanger::boarding::component)
