#include <std_include.hpp>
#include "game/game.hpp"
#include "physical.hpp"
#include "physical_script.hpp"
#include "runtime.hpp"
#include "level_rules.hpp"
#include "../../climb_checkpoint.hpp"
#include "../../climb_collision.hpp"
#include "../../climb_exit.hpp"
#include "../../script_function_extent.hpp"
#include "../../native_player_life.hpp"
#include "../../native_carry.hpp"
#include "../../hands/position_offset.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "../../../head_pose_bridge.hpp"
#include "component/gsc/script_extension.hpp"
#include "component/gsc/script_loading.hpp"
#include "component/scripting.hpp"
#include "component/notifies.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/scripting/execution.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::cliffhanger_physical
{
	namespace
	{
		using namespace hands;
		using clock = controller_input::clock;
		namespace props = equipment::special::cliffhanger;
		using hands::pose_math::compose;
		using hands::pose_math::inverse;
		std::atomic_bool alive{true};
		std::mutex state_mutex, publication_mutex, render_mutex;
		presentation published;
		struct render_frame
		{
			std::array<anchor, 2> wrists{};
			std::uint64_t reference{};
			clock::time_point at{};
		} rendered;
		phase stage{};
		int body = -1, carrier = -1, result{}, milestones{}, last_time{};
		bool final{}, faulted{}, hooks_ready{}, trace_ready{};
		weapons::native_carry::world_key body_key{}, carrier_key{};
		vec start{}, finish{}, position{}, low{}, high{};
		float units{};
		free_climb::solver control;
		controller_input::frame previous_input;
		std::array<free_climb::hand, 2> previous_hands{};
		std::uint64_t sequence{}, reference{}, continuity{}, moves{}, blocked{}, snaps{}, extracts{};
		std::string reason = "waiting for native climb entry", registration = "waiting for scripts",
		            fall_side = "right";
		std::string entry_failure, last_surface;
		std::array<free_climb::contact, 2> entry_hits{};
		std::array<quat, 2> entry_rotations{};
		std::uint64_t entry_attempts{}, entry_rejections{}, entry_contacts{}, entry_without_support{};
		std::uint64_t restores{}, slides{};
		vec collision_normal{};
		std::pair<const char*, const char*> first_entry_range{}, final_entry_range{};
		template <class T> T read(const void* p, std::size_t offset)
		{
			T v{};
			std::memcpy(&v, static_cast<const std::byte*>(p) + offset, sizeof(v));
			return v;
		}
		scripting::script_value argument(unsigned i)
		{
			return i < game::scr_VmPub->outparamcount ? scripting::script_value{game::scr_VmPub->top[-int(i)]}
			                                          : scripting::script_value{};
		}
		bool fresh(clock::time_point at)
		{
			const auto now = clock::now();
			return at != clock::time_point{} && now >= at && now - at <= 150ms;
		}
		bool supported()
		{
			const auto* d = game::Dvar_FindVar("mapname");
			return d && d->current.string && std::string_view(d->current.string) == "cliffhanger";
		}
		bool configured()
		{
			const auto* vr = game::Dvar_FindVar("vr_enable");
			const auto* hands = game::Dvar_FindVar("vr_independentHands");
			return alive && trace_ready && supported() && vr && vr->current.enabled && hands &&
			       hands->current.enabled;
		}
		int parent()
		{
			const auto* link = read<const void*>(&game::g_entities[0], 0x208);
			const auto* p = link ? read<const game::gentity_s*>(link, 0) : nullptr;
			if (!p)
				return -1;
			const auto delta = reinterpret_cast<std::uintptr_t>(p) -
			                   reinterpret_cast<std::uintptr_t>(game::g_entities.get());
			return delta % sizeof(game::gentity_s) == 0 &&
			               delta / sizeof(game::gentity_s) < game::ENTITYNUM_WORLD
			           ? int(delta / sizeof(game::gentity_s))
			           : -1;
		}
		bool session_ready()
		{
			bool dead{};
			const auto* ps = game::g_entities[0].client;
			const auto* hands_setting = game::Dvar_FindVar("vr_independentHands");
			return alive && hands_setting && hands_setting->current.enabled && supported() && trace_ready &&
			       props::geometry().assets && game::CL_IsCgameInitialized() &&
			       head_pose_bridge::get_status().enabled && ps && player_life::read(ps, dead) && !dead;
		}
		bool eligible()
		{
			// Claim script ownership from configuration. Transient asset/HMD
			// readiness after load must wait inside our bridge, not fall through
			// into an unhookable, already-running native climb loop.
			if (!configured())
				return false;
			const int owner = parent();
			if (owner <= 0)
				return false;
			try
			{
				return scripting::entity{game::scr_entref_t{static_cast<unsigned short>(owner), 0}}
				           .get("animname")
				           .as<std::string>() == "worldbody";
			}
			catch (const std::exception&)
			{
				return false;
			}
		}
		bool entry_available()
		{
			const auto input = controller_input::latest_interaction();
			const auto* paused = game::Dvar_FindVar("cl_paused");
			head_pose_bridge::spatial_frame spatial;
			if (!paused || paused->current.integer || *game::keyCatchers || !input.focused ||
			    input.orientation_settling || !fresh(input.sampled_at))
				return false;
			for (unsigned h = 0; h < 2; ++h)
				if (!input.grip[h].valid || !input.aim[h].valid || !input.runtime_grip[h].valid ||
				    !input.runtime_aim[h].valid)
					return false;
			return head_pose_bridge::get_spatial_frame(spatial) && fresh(spatial.captured_at) &&
			       spatial.generation == input.reference_generation;
		}
		std::pair<const char*, const char*> range(std::string_view function)
		{
			const auto entry = reinterpret_cast<std::uintptr_t>(
			    scripting::get_function_pos("_id_BB6C", std::string(function)));
			for (const auto& [file, entries] : scripting::script_function_table_sort)
				if (const auto span = script_function_extent(entries, entry))
					return {reinterpret_cast<const char*>(span->first),
					        reinterpret_cast<const char*>(span->second)};
			return {};
		}
		bool called_from(std::pair<const char*, const char*> span)
		{
			const auto a = reinterpret_cast<std::uintptr_t>(span.first),
			           b = reinterpret_cast<std::uintptr_t>(span.second);
			const auto* frame = game::scr_VmPub->function_frame;
			const auto* first = game::scr_VmPub->function_frame_start;
			if (!a || b <= a || frame <= first || frame - first > 31)
				return false;
			const auto pos = reinterpret_cast<std::uintptr_t>((frame - 1)->fs.pos);
			return pos >= a && pos < b;
		}
		bool right_guard()
		{
			return configured() && called_from(first_entry_range);
		}
		bool left_guard()
		{
			return eligible() && called_from(final_entry_range);
		}
		bool failure_guard()
		{
			return eligible() && called_from(final_entry_range) && !(session_ready() && entry_available());
		}
		void install_hooks()
		{
			if (hooks_ready || !supported())
				return;
			try
			{
				const auto source = scripting::get_function_pos("_id_BB6C", "_id_B33F");
				(void)scripting::get_function_pos("_id_BB6C", "_id_BA39");
				(void)scripting::get_function_pos("_id_BB6C", "_id_CDFC");
				const auto right = scripting::get_function_pos("_id_BB6C", "_id_B833");
				const auto left = scripting::get_function_pos("_id_BB6C", "_id_D1F4");
				const auto failure = scripting::get_function_pos("_id_BB6C", "_id_CC03");
				first_entry_range = range("_id_D231");
				final_entry_range = range("_id_BFDE");
				if (!first_entry_range.first || !final_entry_range.first)
				{
					registration = "native function extent missing";
					return;
				}
				const auto replacement = scripting::get_function_pos(std::string(script_name), "climb");
				const auto r = scripting::get_function_pos(std::string(script_name), "entry_right");
				const auto l = scripting::get_function_pos(std::string(script_name), "entry_left");
				const auto f = scripting::get_function_pos(std::string(script_name), "entry_failure");
				notifies::set_gsc_hook(source, replacement, eligible);
				notifies::set_gsc_hook(right, r, right_guard);
				notifies::set_gsc_hook(left, l, left_guard);
				notifies::set_gsc_hook(failure, f, failure_guard);
				hooks_ready = true;
				registration = "native entry and handoff bridge registered";
			}
			catch (const std::exception& e)
			{
				registration = e.what();
			}
		}
		void reset()
		{
			stage = {};
			body = carrier = -1;
			result = milestones = last_time = 0;
			faulted = false;
			control.reset();
			sequence = reference = continuity = 0;
			reason = "waiting for native climb entry";
			entry_hits = {};
			entry_rotations = {};
			{
				const std::lock_guard lock(publication_mutex);
				published = {};
			}
			{
				const std::lock_guard lock(render_mutex);
				rendered = {};
			}
		}
		bool valid_story_body(const scripting::entity& actor)
		{
			const auto id = actor.get_entity_id();
			if (!id || id >= 56320 ||
			    game::scr_VarGlob->objectVariableValue[id].w.type != game::SCRIPT_ENTITY)
				return false;
			const scripting::entity level{*game::levelEntityId};
			const auto owner = level.get("_id_C374"), name = actor.get("animname");
			return owner.is<scripting::entity>() && owner.as<scripting::entity>() == actor &&
			       name.is<std::string>() && name.as<std::string>() == "worldbody";
		}
		scripting::vector script_vector(vec p)
		{
			return {p[0], p[1], p[2]};
		}
		bool vector_field(const scripting::array& data, const char* key, vec& out)
		{
			const auto value = data.get(std::string(key));
			if (!value.is<scripting::vector>())
				return false;
			const auto p = value.as<scripting::vector>();
			out = {p[0], p[1], p[2]};
			return free_climb::finite(out);
		}
		scripting::array save_supports(const std::array<free_climb::saved_support, 2>& hands)
		{
			scripting::array out;
			for (const auto& h : hands)
			{
				scripting::array item;
				item.set(std::string("valid"), int(h.valid));
				if (h.valid)
				{
					item.set(std::string("point"), script_vector(h.point_units));
					item.set(std::string("normal"), script_vector(h.normal));
					item.set(std::string("rotation"),
					         script_vector({h.rotation[0], h.rotation[1], h.rotation[2]}));
					item.set(std::string("w"), h.rotation[3]);
				}
				out.push(item);
			}
			return out;
		}
		std::array<free_climb::saved_support, 2> load_supports(const scripting::script_value& value)
		{
			std::array<free_climb::saved_support, 2> out{};
			if (!value.is<scripting::array>())
				return out;
			const auto saved = value.as<scripting::array>();
			for (unsigned h = 0; h < 2; ++h)
			{
				const auto raw = saved.get(h);
				if (!raw.is<scripting::array>())
					continue;
				const auto item = raw.as<scripting::array>();
				const auto valid = item.get(std::string("valid"));
				if (!valid.is<int>() || !valid.as<int>())
					continue;
				vec rotation{};
				const auto w = item.get(std::string("w"));
				auto& state = out[h];
				if (!vector_field(item, "point", state.point_units) ||
				    !vector_field(item, "normal", state.normal) ||
				    !vector_field(item, "rotation", rotation) || !w.is<float>())
					continue;
				state.rotation = {rotation[0], rotation[1], rotation[2], w.as<float>()};
				state.valid =
				    std::abs(length(state.normal) - 1.f) < .01f && free_climb::rotation_valid(state.rotation);
			}
			return out;
		}
		void persist()
		{
			if (carrier <= 0 || body <= 0)
				return;
			const scripting::entity proxy{game::scr_entref_t{static_cast<unsigned short>(carrier), 0}};
			const scripting::entity actor{game::scr_entref_t{static_cast<unsigned short>(body), 0}};
			scripting::array data;
			data.set(std::string("body"), actor);
			data.set(std::string("start"), script_vector(start));
			data.set(std::string("finish"), script_vector(finish));
			data.set(std::string("final"), int(final));
			data.set(std::string("milestones"), milestones);
			data.set(std::string("result"), result);
			data.set(std::string("fall_side"), fall_side);
			data.set(std::string("hands"), save_supports(free_climb::checkpoint(control, units)));
			// One VM-owned value replacement: native saves serialize this with
			// the proxy, whereas process globals and tracker baselines are lost.
			proxy.set("vr_climb_save", data);
			if (result)
				actor.set("vr_climb_handoff", 1);
		}
		void publish_supports()
		{
			const auto geometry = props::geometry();
			presentation view;
			view.stage = stage;
			view.body = body;
			view.carrier = carrier;
			view.reference = reference;
			view.units = units;
			view.latched = control.held();
			for (unsigned h = 0; h < 2; ++h)
				if (control.hands[h].surface.valid)
				{
					const auto& s = control.hands[h];
					const auto local =
					    compose(geometry.attachment[h], {geometry.tip[h], {0, 0, 0, 1}}).position;
					view.wrists[h] = {sub(scale(s.surface.point, units), rotate(s.rotation, local)),
					                  s.rotation};
				}
			const std::lock_guard lock(publication_mutex);
			published = view;
		}
		bool restore_current()
		{
			if (stage != phase::inactive || !configured() || !game::CL_IsCgameInitialized() ||
			    !*game::levelEntityId)
				return false;
			const int owner = parent();
			if (owner <= 0)
				return false;
			const scripting::entity proxy{game::scr_entref_t{static_cast<unsigned short>(owner), 0}};
			const auto name = proxy.get("animname");
			if (!name.is<std::string>())
				return false;
			if (name.as<std::string>() == "worldbody")
			{
				const auto handoff = proxy.get("vr_climb_handoff");
				if (!handoff.is<int>() || !handoff.as<int>() || !valid_story_body(proxy))
					return false;
				body = owner;
				body_key = weapons::native_carry::entity_key(body);
				stage = phase::authored_exit;
				publish_supports();
				++restores;
				return true;
			}
			if (name.as<std::string>() != "vr_climb_carrier")
				return false;
			const auto stored = proxy.get("vr_climb_save");
			if (!stored.is<scripting::array>())
				return false;
			const auto data = stored.as<scripting::array>();
			const auto actor_value = data.get(std::string("body"));
			if (!actor_value.is<scripting::entity>())
				return false;
			const auto actor = actor_value.as<scripting::entity>();
			const auto f = data.get(std::string("final")), m = data.get(std::string("milestones")),
			           r = data.get(std::string("result"));
			vec saved_start{}, saved_finish{};
			if (!valid_story_body(actor) || !vector_field(data, "start", saved_start) ||
			    !vector_field(data, "finish", saved_finish) ||
			    length(sub(saved_start, saved_finish)) > 1600 || !f.is<int>() || !m.is<int>() || !r.is<int>())
				return false;
			const auto scale = head_pose_bridge::get_status().world_scale;
			if (!std::isfinite(scale) || scale < .01f || scale > 10000)
				return false;
			free_climb::solver recovered;
			if (!free_climb::restore(recovered, load_supports(data.get(std::string("hands"))), scale))
				return false;
			body = actor.get_entity_reference().entnum;
			carrier = owner;
			body_key = weapons::native_carry::entity_key(body);
			carrier_key = weapons::native_carry::entity_key(carrier);
			start = saved_start;
			finish = saved_finish;
			position = read<vec>(&game::g_entities[carrier], 0x1c);
			units = scale;
			final = f.as<int>() != 0;
			milestones = m.as<int>() & 7;
			result = std::clamp(r.as<int>(), -1, 1);
			const auto side = data.get(std::string("fall_side"));
			fall_side = side.is<std::string>() && side.as<std::string>() == "left" ? "left" : "right";
			control = recovered;
			last_time = 0;
			sequence = reference = continuity = 0;
			faulted = false;
			for (unsigned i = 0; i < 3; ++i)
			{
				low[i] = std::min(start[i], finish[i]) - 96;
				high[i] = std::max(start[i], finish[i]) + 96;
			}
			stage = result ? phase::authored_exit : phase::climbing;
			if (!result)
			{
				actor.call("hide");
				actor.call("notsolid");
				proxy.call("hide");
				proxy.call("notsolid");
				const scripting::entity player{game::scr_entref_t{0, 0}};
				player.call("playersetgroundreferenceent", {proxy});
			}
			publish_supports();
			++restores;
			reason = "checkpoint restored; waiting for fresh tracking baselines";
			return true;
		}
		bool restore_script_context()
		{
			if (stage != phase::inactive || game::scr_VmPub->outparamcount < 5 || !configured() ||
			    !game::CL_IsCgameInitialized())
				return false;
			const auto actor = argument(0).as<scripting::entity>(),
			           proxy = argument(1).as<scripting::entity>();
			const auto id = proxy.get_entity_id();
			if (!valid_story_body(actor) || !id || id >= 56320 ||
			    game::scr_VarGlob->objectVariableValue[id].w.type != game::SCRIPT_ENTITY)
				return false;
			const auto name = proxy.get("animname");
			if (!name.is<std::string>() || name.as<std::string>() != "vr_climb_carrier")
				return false;
			const auto a = argument(3).as<scripting::vector>(), b = argument(4).as<scripting::vector>();
			const vec saved_start{a[0], a[1], a[2]}, saved_finish{b[0], b[1], b[2]};
			if (!free_climb::finite(saved_start) || !free_climb::finite(saved_finish) ||
			    length(sub(saved_start, saved_finish)) > 1600)
				return false;
			const int actor_id = actor.get_entity_reference().entnum,
			          proxy_id = proxy.get_entity_reference().entnum;
			if (parent() != proxy_id && parent() != actor_id)
				return false;
			const auto scale = head_pose_bridge::get_status().world_scale;
			if (!std::isfinite(scale) || scale < .01f || scale > 10000)
				return false;
			if (parent() != proxy_id)
			{
				const scripting::entity player{game::scr_entref_t{0, 0}};
				player.call("unlink");
				player.call("playerlinktodelta", {proxy, "tag_origin", 1, 0, 0, 0, 0, 1});
			}
			// If the native engine resumed the script before the plugin state,
			// its serialized locals are authoritative for this exact carrier.
			// Missing historical contacts do not manufacture a release/fall.
			body = actor_id;
			carrier = proxy_id;
			units = scale;
			start = saved_start;
			finish = saved_finish;
			body_key = weapons::native_carry::entity_key(body);
			carrier_key = weapons::native_carry::entity_key(carrier);
			position = read<vec>(&game::g_entities[carrier], 0x1c);
			final = game::Scr_GetInt(2) != 0;
			stage = phase::climbing;
			result = milestones = last_time = 0;
			sequence = reference = continuity = 0;
			faulted = false;
			control.reset();
			const scripting::entity level{*game::levelEntityId};
			const auto flags = level.get("flag");
			if (flags.is<scripting::array>())
			{
				const auto values = flags.as<scripting::array>();
				const std::array names{
				    "player_begins_to_climb", "price_climb_continues", "player_climbed_3_steps"};
				for (unsigned i = 0; i < 3; ++i)
				{
					const auto flag = values.get(std::string(names[i]));
					if (flag.is<int>() && flag.as<int>())
						milestones |= 1u << i;
				}
			}
			for (unsigned i = 0; i < 3; ++i)
			{
				low[i] = std::min(start[i], finish[i]) - 96;
				high[i] = std::max(start[i], finish[i]) + 96;
			}
			actor.call("hide");
			actor.call("notsolid");
			proxy.call("hide");
			proxy.call("notsolid");
			const scripting::entity player{game::scr_entref_t{0, 0}};
			player.call("playersetgroundreferenceent", {proxy});
			persist();
			publish_supports();
			++restores;
			reason = "script context restored without saved contacts; waiting for real support";
			return true;
		}
		int query_state()
		{
			const std::lock_guard lock(state_mutex);
			if (stage == phase::climbing && game::g_entities[0].client &&
			    read<int>(game::g_entities[0].client, 0x4c) < last_time)
				reset();
			if (stage == phase::inactive && !restore_current())
				restore_script_context();
			// A loading frame is not a deliberate release. The resumed GSC
			// thread waits here until its validated context can be rebound.
			return stage == phase::inactive ? 0 : result;
		}
		bool in_region(vec point)
		{
			for (unsigned i = 0; i < 3; ++i)
				if (point[i] < low[i] || point[i] > high[i])
					return false;
			return true;
		}
		free_climb::contact surface(vec from, vec to, bool region = true, bool climbable = true)
		{
			if (!free_climb::finite(from) || !free_climb::finite(to) || length(sub(to, from)) < .001f)
				return {};
			const auto vecarg = [](vec p) { return scripting::vector{p[0], p[1], p[2]}; };
			// Same model trace and contents policy as authored ice-pick hits.
			const auto value =
			    scripting::call("bullettrace", {vecarg(from), vecarg(to), 0, scripting::script_value{}});
			if (!value.is<scripting::array>())
				return {};
			const auto hit = value.as<scripting::array>();
			const auto f = hit.get(std::string("fraction")), p = hit.get(std::string("position")),
			           n = hit.get(std::string("normal"));
			const auto material = hit.get(std::string("surfacetype")),
			           entity = hit.get(std::string("entity"));
			if (!f.is<float>() || f.as<float>() <= 0 || f.as<float>() >= 1 || !p.is<scripting::vector>() ||
			    !n.is<scripting::vector>() || !material.is<std::string>() || entity.is<scripting::entity>())
				return {};
			const auto name = material.as<std::string>();
			last_surface = name;
			if (climbable && !native_climb_material(name))
				return {};
			const auto point = p.as<scripting::vector>(), normal = n.as<scripting::vector>();
			free_climb::contact out{{point[0], point[1], point[2]}, {normal[0], normal[1], normal[2]}, true};
			if (!free_climb::finite(out.point) || !free_climb::finite(out.normal) ||
			    length(out.normal) < .9f || (region && !in_region(out.point)))
				return {};
			out.normal = unit(out.normal);
			return out;
		}
		anchor native_pick(const scripting::entity& e, unsigned h)
		{
			const auto tag = h ? "tag_weapon_right" : "tag_weapon_left";
			const auto p = e.call("gettagorigin", {tag}).as<scripting::vector>(),
			           a = e.call("gettagangles", {tag}).as<scripting::vector>();
			const auto f = scripting::call("anglestoforward", {a}).as<scripting::vector>(),
			           u = scripting::call("anglestoup", {a}).as<scripting::vector>();
			const vec forward{f[0], f[1], f[2]}, up{u[0], u[1], u[2]};
			return {{p[0], p[1], p[2]}, from_axis({forward, unit(cross(up, forward)), up})};
		}
		bool record_entry_contact()
		{
			const std::lock_guard lock(state_mutex);
			const auto actor = argument(0).as<scripting::entity>();
			if (stage == phase::inactive && configured() && valid_story_body(actor) &&
			    actor.get_entity_reference().entnum == parent())
			{
				body = actor.get_entity_reference().entnum;
				body_key = weapons::native_carry::entity_key(body);
				stage = phase::authored_entry;
				publish_supports();
			}
			if (stage != phase::authored_entry || actor.get_entity_reference().entnum != body)
				return false;
			const auto side = argument(1).as<std::string>();
			if (side != "left" && side != "right")
				return false;
			const unsigned h = side == "left" ? 0u : 1u;
			const auto p = argument(2).as<scripting::vector>(), n = argument(3).as<scripting::vector>();
			free_climb::contact hit{{p[0], p[1], p[2]}, {n[0], n[1], n[2]}, true};
			if (!free_climb::finite(hit.point) || !free_climb::finite(hit.normal) || length(hit.normal) < .9f)
				return false;
			hit.normal = unit(hit.normal);
			const auto tool = native_pick(actor, h);
			const auto geometry = props::geometry();
			entry_rotations[h] = compose(tool, inverse(geometry.attachment[h])).rotation;
			entry_hits[h] = hit;
			++entry_contacts;
			auto saved = load_supports(actor.get("vr_climb_entry"));
			saved[h] = {hit.point, hit.normal, entry_rotations[h], true};
			actor.set("vr_climb_entry", save_supports(saved));
			return true;
		}
		bool reject_entry(const char* message)
		{
			++entry_rejections;
			entry_failure = message;
			reason = message;
			console::error("[VR climb entry] %s (attempt=%llu contacts=%llu)\n",
			               message,
			               static_cast<unsigned long long>(entry_attempts),
			               static_cast<unsigned long long>(entry_contacts));
			return false;
		}
		bool prepare()
		{
			const std::lock_guard lock(state_mutex);
			const auto actor = argument(0).as<scripting::entity>();
			const auto owner = actor.get_entity_reference().entnum;
			if (!eligible() || owner != parent())
				return false;
			reset();
			body = owner;
			body_key = weapons::native_carry::entity_key(body);
			stage = phase::authored_entry;
			const std::lock_guard pub(publication_mutex);
			published.stage = stage;
			published.body = body;
			return true;
		}
		bool begin()
		{
			const std::lock_guard lock(state_mutex);
			++entry_attempts;
			const auto actor = argument(0).as<scripting::entity>(),
			           proxy = argument(1).as<scripting::entity>();
			if (stage == phase::inactive && configured() && valid_story_body(actor) &&
			    proxy.get_entity_reference().entnum == parent())
			{
				body = actor.get_entity_reference().entnum;
				body_key = weapons::native_carry::entity_key(body);
				stage = phase::authored_entry;
				const auto saved = load_supports(actor.get("vr_climb_entry"));
				for (unsigned h = 0; h < 2; ++h)
					if (saved[h].valid)
					{
						entry_hits[h] = {saved[h].point_units, saved[h].normal, true};
						entry_rotations[h] = saved[h].rotation;
					}
			}
			if (!session_ready() || stage != phase::authored_entry ||
			    actor.get_entity_reference().entnum != body ||
			    proxy.get_entity_reference().entnum != parent())
				return reject_entry("entry identity, resources or player link rejected");
			carrier = proxy.get_entity_reference().entnum;
			carrier_key = weapons::native_carry::entity_key(carrier);
			final = game::Scr_GetInt(2) != 0;
			const auto side = argument(3).as<std::string>();
			const unsigned h = side == "left" ? 0u : 1u;
			const auto destination = argument(4).as<scripting::vector>();
			finish = {destination[0], destination[1], destination[2]};
			start = position = read<vec>(&game::g_entities[carrier], 0x1c);
			if (!free_climb::finite(start) || !free_climb::finite(finish) ||
			    length(sub(start, finish)) > 1600)
				return reject_entry("entry/exit position rejected");
			for (unsigned i = 0; i < 3; ++i)
			{
				low[i] = std::min(start[i], finish[i]) - 96;
				high[i] = std::max(start[i], finish[i]) + 96;
			}
			head_pose_bridge::spatial_frame spatial;
			if (!head_pose_bridge::get_spatial_frame(spatial) || !std::isfinite(spatial.units_per_meter) ||
			    spatial.units_per_meter < .01f || spatial.units_per_meter > 10000)
				return reject_entry("entry spatial reference unavailable");
			units = spatial.units_per_meter;
			// The native stab callback supplies its actual impact. An idle pose
			// query occurs later and is not evidence of the original contact.
			auto seed = entry_hits[h];
			const auto geometry = props::geometry();
			const auto rotation = entry_rotations[h];
			const bool seeded = seed.valid && in_region(seed.point);
			control.reset();
			if (seeded)
			{
				seed.point = scale(seed.point, 1 / units);
				control.seed(h, seed, rotation);
			}
			else
			{
				++entry_without_support;
				entry_failure =
				    "authored contact absent/outside region; holding entry until a real VR contact";
			}
			stage = phase::climbing;
			result = milestones = 0;
			last_time = 0;
			sequence = reference = continuity = 0;
			presentation view;
			view.stage = stage;
			view.body = body;
			view.carrier = carrier;
			view.units = units;
			view.reference = spatial.generation;
			view.latched = control.held();
			const auto tip_local = compose(geometry.attachment[h], {geometry.tip[h], {0, 0, 0, 1}}).position;
			if (seeded)
				view.wrists[h] = {sub(scale(seed.point, units), rotate(rotation, tip_local)), rotation};
			{
				const std::lock_guard pub(publication_mutex);
				published = view;
			}
			persist();
			reason = "independent VR climb owns proxy; native body is hidden";
			return true;
		}
		void tick()
		{
			const std::lock_guard lock(state_mutex);
			install_hooks();
			if (stage == phase::inactive)
				restore_current();
			if (stage == phase::authored_exit)
			{
				if (body > 0 && (weapons::native_carry::entity_key(body) != body_key ||
				                 (parent() != body && parent() != carrier)))
					reset();
				return;
			}
			if (stage != phase::climbing || faulted)
				return;
			bool dead{};
			const auto* ps = game::g_entities[0].client;
			if (!supported() || !ps || !player_life::read(ps, dead) || dead || parent() != carrier ||
			    weapons::native_carry::entity_key(carrier) != carrier_key ||
			    weapons::native_carry::entity_key(body) != body_key)
			{
				reset();
				return;
			}
			if (!session_ready())
			{
				control.rebase();
				sequence = 0;
				reason = "waiting for restored VR resources; supports retained";
				return;
			}
			const auto input = controller_input::latest_interaction();
			head_pose_bridge::spatial_frame spatial;
			if (!entry_available() || !head_pose_bridge::get_spatial_frame(spatial))
			{
				control.rebase();
				sequence = 0;
				reason = "tracking suspended; ice supports retained";
				return;
			}
			if (!std::isfinite(spatial.units_per_meter) || spatial.units_per_meter < .01f ||
			    spatial.units_per_meter > 10000)
			{
				control.rebase();
				sequence = 0;
				return;
			}
			const int time = read<int>(game::g_entities[0].client, 0x4c);
			if (time < last_time)
			{
				reset();
				restore_current();
				return;
			}
			if (time <= last_time || input.sequence == sequence)
				return;
			const float dt = last_time ? float(time - last_time) * .001f : .016f;
			last_time = time;
			if (reference != input.reference_generation || continuity != input.continuity_generation)
			{
				control.rebase();
				sequence = 0;
				reference = input.reference_generation;
				continuity = input.continuity_generation;
			}
			if (units != spatial.units_per_meter)
			{
				control.rescale(units / spatial.units_per_meter);
				units = spatial.units_per_meter;
				sequence = 0;
			}
			const auto geometry = props::geometry();
			if (!geometry.assets || !geometry.hands_ready)
			{
				control.rebase();
				sequence = 0;
				reason = "waiting for independent hand/prop geometry";
				return;
			}
			const auto setting = [](const char* key, float fallback)
			{
				const auto* d = game::Dvar_FindVar(key);
				return d ? d->current.value : fallback;
			};
			position_offsets offsets;
			offsets = {setting(vr::settings::active_hand_alignment()[0].name, offsets.inward_meters),
			           setting(vr::settings::active_hand_alignment()[1].name, offsets.back_meters),
			           setting(vr::settings::active_hand_alignment()[2].name, offsets.up_meters)};
			render_frame skin;
			{
				const std::lock_guard r(render_mutex);
				skin = rendered;
			}
			std::array<free_climb::hand, 2> hands{};
			std::array<free_climb::contact, 2> contacts{}, walls{};
			for (unsigned h = 0; h < 2; ++h)
			{
				anchor wrist;
				if (!tracked_wrist(input, spatial, {}, h, offsets, wrist))
					continue;
				const auto raw_wrist = wrist;
				wrist.rotation = normalize(multiply(wrist.rotation, geometry.basis[h]));
				const auto tip_local =
				    compose(geometry.attachment[h], {geometry.tip[h], {0, 0, 0, 1}}).position;
				auto tip = add(wrist.position, rotate(wrist.rotation, tip_local));
				if (skin.reference == reference && fresh(skin.at))
					tip = add(skin.wrists[h].position, rotate(skin.wrists[h].rotation, tip_local));
				hands[h] = {scale(wrist.position, 1 / units),
				            scale(tip, 1 / units),
				            wrist.rotation,
				            true,
				            free_climb::fixed(input.trigger[h].active,
				                              input.trigger[h].down,
				                              input.squeeze[h].active,
				                              input.squeeze[h].down)};
				if (sequence && previous_hands[h].valid)
				{
					anchor previous;
					if (tracked_wrist(previous_input, spatial, {}, h, offsets, previous))
						hands[h].motion = add(previous_hands[h].motion,
						                      scale(sub(raw_wrist.position, previous.position), 1 / units));
					if (!control.hands[h].surface.valid)
					{
						const auto from = scale(previous_hands[h].tip, units), travel = sub(tip, from);
						if (length(travel) > .02f)
						{
							const auto hit = surface(from, add(tip, scale(unit(travel), .025f * units)));
							if (hit.valid)
							{
								contacts[h] = hit;
								contacts[h].point = scale(hit.point, 1 / units);
							}
						}
					}
				}
				hands[h].intent = add(hands[h].motion, scale(rotate(wrist.rotation, tip_local), 1 / units));
				const auto raw_tip = add(wrist.position, rotate(wrist.rotation, tip_local));
				walls[h] = surface(spatial.head_position, raw_tip, false, false);
				if (!control.hands[h].surface.valid && !contacts[h].valid && walls[h].valid &&
				    length(sub(tip, walls[h].point)) <= .06f * units)
				{
					contacts[h] = surface(spatial.head_position, raw_tip);
					if (contacts[h].valid)
						contacts[h].point = scale(contacts[h].point, 1 / units);
				}
			}
			if (!hands[0].valid || !hands[1].valid)
			{
				control.rebase();
				sequence = 0;
				return;
			}
			position = read<vec>(&game::g_entities[carrier], 0x1c);
			const int previous_milestones = milestones;
			const auto response = control.update(scale(position, 1 / units), reference, dt, hands, contacts);
			snaps += bool(response.attached);
			extracts += bool(response.detached);
			if (response.attached)
			{
				const scripting::entity player{game::scr_entref_t{0, 0}};
				player.call("playsound", {"icepick_impact_ice"});
			}
			if (response.detached)
				fall_side = response.detached & 2 ? "right" : "left";
			if (response.falling)
			{
				result = -1;
				stage = phase::authored_exit;
				reason = "released last support; native fall handoff";
			}
			else
			{
				const auto desired =
				    scale(free_climb::bounded_step(scale(position, 1 / units), response.goal, dt), units);
				const auto delta = sub(desired, position);
				if (length(delta) > .001f)
				{
					const auto from = read<vec>(&game::g_entities[0], 0x1c);
					const auto standing = read<game::Bounds>(&game::g_entities[0], 0xc0);
					free_climb::volume suspended;
					if (!free_climb::hanging_volume(
					        {{standing.midPoint[0], standing.midPoint[1], standing.midPoint[2]},
					         {standing.halfSize[0], standing.halfSize[1], standing.halfSize[2]}},
					        units,
					        suspended))
						throw std::runtime_error("native player collision bounds invalid");
					game::Bounds bounds{};
					std::copy(suspended.midpoint.begin(), suspended.midpoint.end(), bounds.midPoint);
					std::copy(suspended.half.begin(), suspended.half.end(), bounds.halfSize);
					const auto collision = free_climb::slide(
					    from,
					    delta,
					    .005f * units,
					    [&](vec a, vec b)
					    {
						    game::trace_t hit{};
						    game::G_TraceCapsule(&hit, a.data(), b.data(), &bounds, 0, 0x280e831);
						    return free_climb::sweep_hit{hit.fraction,
						                                 {hit.normal[0], hit.normal[1], hit.normal[2]},
						                                 bool(hit.startsolid),
						                                 bool(hit.allsolid)};
					    });
					const auto accepted = add(position, sub(collision.position, from));
					if (length(sub(accepted, position)) > .001f)
					{
						const scripting::entity proxy{
						    game::scr_entref_t{static_cast<unsigned short>(carrier), 0}};
						const auto fields = scripting::fields_table.find(0);
						if (fields == scripting::fields_table.end() || !fields->second.contains("origin"))
							throw std::runtime_error("native origin field is unavailable");
						proxy.set("origin", scripting::vector{accepted[0], accepted[1], accepted[2]});
						position = read<vec>(&game::g_entities[carrier], 0x1c);
						if (!free_climb::finite(position) || length(sub(position, accepted)) > .02f)
							throw std::runtime_error("native carrier movement was rejected");
						++moves;
						if (collision.blocked)
							++slides;
					}
					if (collision.blocked)
					{
						++blocked;
						const auto actual = scale(position, 1 / units);
						if (collision.plane_count)
							collision_normal = collision.planes[collision.plane_count - 1];
						if (collision.stuck)
							control.obstructed(actual);
						else
							for (auto& h : control.hands)
								if (h.surface.valid)
									h.goal = add(actual,
									             free_climb::clip_motion(
									                 sub(h.goal, actual),
									                 {collision.planes.data(), collision.plane_count}));
					}
				}
				const float rise = position[2] - start[2];
				if (!final && rise > 4)
					milestones |= 1;
				if (!final && rise >= 70)
					milestones |= 2;
				if (!final && rise >= 100)
					milestones |= 4;
				if (control.held() &&
				    free_climb::reached_exit(scale(position, 1 / units), scale(finish, 1 / units)))
				{
					result = 1;
					stage = phase::authored_exit;
					reason = "entered authored exit region";
				}
				else
					reason = "free ice contact; independent proxy follows absolute hand support";
			}
			if (response.attached || response.detached || milestones != previous_milestones || result)
				persist();
			presentation view;
			view.stage = stage;
			view.body = body;
			view.carrier = carrier;
			view.reference = reference;
			view.units = units;
			view.latched = control.held();
			view.walls = walls;
			for (unsigned h = 0; h < 2; ++h)
			{
				if (control.hands[h].surface.valid)
				{
					const auto& s = control.hands[h];
					const auto tip_local =
					    compose(geometry.attachment[h], {geometry.tip[h], {0, 0, 0, 1}}).position;
					const auto point =
					    scale(add(s.surface.point, scale(s.surface.normal, s.withdrawal)), units);
					view.wrists[h] = {sub(point, rotate(s.rotation, tip_local)), s.rotation};
				}
			}
			{
				const std::lock_guard pub(publication_mutex);
				published = view;
			}
			previous_input = input;
			previous_hands = hands;
			sequence = input.sequence;
		}
		void safe_tick()
		{
			try
			{
				tick();
			}
			catch (const std::exception& e)
			{
				const std::lock_guard lock(state_mutex);
				faulted = true;
				control.rebase();
				reason = std::string("free climb suspended: ") + e.what();
				console::error("[VR climb] %s\n", reason.c_str());
			}
		}
	}
	presentation latest() noexcept
	{
		const std::lock_guard lock(publication_mutex);
		return published;
	}
	bool owns_movement() noexcept
	{
		return latest().active();
	}
	bool independent_hands() noexcept
	{
		return latest().stage == phase::climbing;
	}
	bool preserve_native_arms(int owner) noexcept
	{
		const auto p = latest();
		return p.body == owner && p.authored();
	}
	void constrain_hands(std::array<anchor, 2>& targets,
	                     const std::array<quat, 2>& basis,
	                     vec offset,
	                     std::uint64_t ref) noexcept
	{
		const auto view = latest();
		if (view.stage != phase::climbing || view.reference != ref)
			return;
		const auto geometry = props::geometry();
		for (unsigned h = 0; h < 2; ++h)
			if (view.latched & (1u << h))
				targets[h] = {sub(view.wrists[h].position, offset),
				              normalize(multiply(view.wrists[h].rotation, conjugate(basis[h])))};
			else if (view.walls[h].valid && geometry.assets)
			{
				auto wrist = free_climb::outside_surface(
				    add(targets[h].position, offset), view.walls[h], .07f * view.units);
				const auto local = compose(geometry.attachment[h], {geometry.tip[h], {0, 0, 0, 1}}).position;
				const auto tip =
				    add(wrist, rotate(normalize(multiply(targets[h].rotation, basis[h])), local));
				wrist =
				    add(wrist, sub(free_climb::outside_surface(tip, view.walls[h], .002f * view.units), tip));
				targets[h].position = sub(wrist, offset);
			}
	}
	void publish_hands(const controller_input::frame& input,
	                   const std::array<anchor, 2>& wrists,
	                   vec offset) noexcept
	{
		if (!independent_hands())
			return;
		render_frame frame{wrists, input.reference_generation, input.sampled_at};
		for (auto& w : frame.wrists)
			w.position = add(w.position, offset);
		const std::lock_guard lock(render_mutex);
		rendered = frame;
	}
	std::string status()
	{
		const std::lock_guard lock(state_mutex);
		return std::format(
		    "free_climb phase={} story_body={} proxy={} held={} result={} milestones={} moves={} blocked={} contacts={} extractions={} root=({},{},{}) exit=({},{},{}) reason={} registration={} entry_attempts={} entry_rejections={} native_entry_contacts={} entry_without_support={} last_entry_failure={} last_surface={} restores={} slides={} collision_normal=({},{},{})\n",
		    int(stage),
		    body,
		    carrier,
		    control.held(),
		    result,
		    milestones,
		    moves,
		    blocked,
		    snaps,
		    extracts,
		    position[0],
		    position[1],
		    position[2],
		    finish[0],
		    finish[1],
		    finish[2],
		    reason,
		    registration,
		    entry_attempts,
		    entry_rejections,
		    entry_contacts,
		    entry_without_support,
		    entry_failure,
		    last_surface,
		    restores,
		    slides,
		    collision_normal[0],
		    collision_normal[1],
		    collision_normal[2]);
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			constexpr std::uint8_t bytes[]{0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x78};
			std::array<std::uint8_t, sizeof(bytes)> mask{};
			mask.fill(255);
			trace_ready = bool(utils::hook_validation::verify_masked_bytes(
			    reinterpret_cast<void*>(0x1404CBFE0), {bytes, mask.data(), sizeof(bytes)}));
			gsc::register_virtual_source(std::string(script_name), std::string(script_source), "cliffhanger");
			gsc::add_function("vrphysicalclimbprepare", [] { game::Scr_AddInt(prepare()); });
			gsc::add_function("vrphysicalclimbseed", [] { game::Scr_AddInt(record_entry_contact()); });
			gsc::add_function("vrphysicalclimbbegin", [] { game::Scr_AddInt(begin()); });
			gsc::add_function("vrphysicalclimbstate", [] { game::Scr_AddInt(query_state()); });
			gsc::add_function("vrphysicalclimbmilestone",
			                  []
			                  {
				                  const std::lock_guard lock(state_mutex);
				                  game::Scr_AddInt(milestones);
			                  });
			gsc::add_function("vrphysicalclimbfallside",
			                  []
			                  {
				                  const std::lock_guard lock(state_mutex);
				                  game::Scr_AddString(fall_side.c_str());
			                  });
			gsc::add_function("vrphysicalclimbentryready",
			                  [] { game::Scr_AddInt(session_ready() && entry_available()); });
			gsc::add_function("vrphysicalclimbend",
			                  []
			                  {
				                  const std::lock_guard lock(state_mutex);
				                  reset();
			                  });
			scripting::on_level_start(
			    []
			    {
				    const std::lock_guard lock(state_mutex);
				    reset();
				    hooks_ready = false;
				    install_hooks();
				    entry_failure.clear();
				    last_surface.clear();
				    entry_attempts = entry_rejections = entry_contacts = entry_without_support = 0;
			    });
			scripting::on_shutdown(
			    [](bool, bool after)
			    {
				    if (!after)
				    {
					    const std::lock_guard lock(state_mutex);
					    reset();
					    hooks_ready = false;
				    }
			    });
			scheduler::loop(safe_tick, scheduler::pipeline::server);
			command::add("vr_cliffhanger_physical_status",
			             []
			             {
				             const auto text = status();
				             console::info("%s", text.c_str());
				             utils::io::write_file_atomic("minidumps/overlord-physical-climb.txt", text);
			             });
		}
		void pre_destroy() override
		{
			alive = false;
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::cliffhanger_physical::component)
