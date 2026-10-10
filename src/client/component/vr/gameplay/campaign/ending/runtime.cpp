#include <std_include.hpp>
#include "../../../h2/entrypoints.hpp"
#include "runtime.hpp"
#include "script.hpp"
#include "grips.hpp"
#include "../../climb_collision.hpp"
#include "../../native_carry.hpp"
#include "../../native_player_life.hpp"
#include "../../hands/native_rig.hpp"
#include "../../forearm_twist.hpp"
#include "../../hands/position_offset.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "../scripted_sequences.hpp"
#include "../../../hud_prompts.hpp"
#include "component/gsc/script_extension.hpp"
#include "component/gsc/script_loading.hpp"
#include "component/scripting.hpp"
#include "component/notifies.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::ending
{
	namespace
	{
		using namespace hands;
		using namespace hands::pose_math;
		using clock = controller_input::clock;
		std::mutex state_mutex, pub_mutex, pose_mutex;
		std::atomic_bool alive{true}, impaled{};
		bool hooks_ready{}, trace_ready{}, ground_ready{}, work_enabled{}, grasp_enabled{}, throw_session{};
		presentation published;
		pull_gesture pull;
		throw_gesture throwing;
		std::uint64_t generation = 1, last_sequence{}, last_reference{}, last_continuity{};
		int last_time{}, interaction_body = -1, interaction_knife = -1;
		float pending_work{};
		anchor knife_in_wrist{};
		weapons::native_carry::world_key interaction_key{};
		std::string reason = "waiting for ending", registration = "waiting for ending scripts";
		std::uint64_t pulls{}, throws{}, moves{}, groundings{}, faults{};
		struct crawl_session
		{
			int body = -1, carrier = -1;
			weapons::native_carry::world_key body_key{}, carrier_key{};
			vec start{}, finish{}, eye_offset{}, goal{};
			float units{};
			bool done{}, goal_valid{}, height_ready{};
			float arrival_distance{};
			int height_started{};
			float height_from{}, height_to{};
			free_climb::solver solver;
			controller_input::frame previous;
			std::array<free_climb::hand, 2> hands{};
			std::array<controller_input::digital_press_gate, 2> grip_edges{};
			std::array<anchor, 2> fixed_wrists{};
			std::uint64_t sequence{}, reference{}, continuity{};
			int time{};
		} crawl;
		struct grounding
		{
			int actor = -1, gun = -1;
			float delta{};
			std::uint64_t lease{};
		} ground;
		std::array<scripted_arms::once_per_pose, 2> grounded_poses;
		std::array<const void*, 2> grounded_objects{};
		std::uint64_t throw_preparations{}, throw_attachment_rejections{};

		template <class T> T read(const void* p, std::size_t offset)
		{
			T v{};
			std::memcpy(&v, static_cast<const std::byte*>(p) + offset, sizeof(v));
			return v;
		}
		bool map_supported()
		{
			const auto* d = game::Dvar_FindVar("mapname");
			return d && d->current.string && std::string_view(d->current.string) == "ending";
		}
		bool configured()
		{
			const auto* vr = game::Dvar_FindVar("vr_enable");
			const auto* hands = game::Dvar_FindVar("vr_independentHands");
			return alive && trace_ready && map_supported() && vr && vr->current.enabled && hands &&
			       hands->current.enabled;
		}
		bool scene_context()
		{
			if (!map_supported() || !game::CL_IsCgameInitialized() || !*game::levelEntityId)
				return false;
			try
			{
				const auto mode = scripting::get_object_variable(*game::levelEntityId, 0xAC38u);
				if (!mode.is<std::string>() || mode.as<std::string>() != "credits_1")
					return false;
				const scripting::entity level{*game::levelEntityId};
				const auto entry = level.get("start_point");
				if (!entry.is<std::string>() || !known_entry(entry.as<std::string>()))
					return false;
				const auto flags = level.get("flag");
				if (!flags.is<scripting::array>())
					return false;
				const auto credits = flags.as<scripting::array>().get(std::string("do_museum_credits"));
				return credits.is<int>() && credits.as<int>() == 0;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}
		bool eligible()
		{
			if (!configured() || !scene_context())
				return false;
			try
			{
				const auto failed = scripting::entity{*game::levelEntityId}.get("missionfailed");
				return !failed.is<int>() || !failed.as<int>();
			}
			catch (const std::exception&)
			{
				return false;
			}
		}
		scripting::script_value argument(unsigned n)
		{
			return n < game::scr_VmPub->outparamcount ? scripting::script_value{game::scr_VmPub->top[-int(n)]}
			                                          : scripting::script_value{};
		}
		int id(const scripting::script_value& v)
		{
			if (!v.is<scripting::entity>())
				return -1;
			const auto e = v.as<scripting::entity>();
			const auto key = e.get_entity_id();
			if (!key || key >= 56320 ||
			    game::scr_VarGlob->objectVariableValue[key].w.type != game::SCRIPT_ENTITY)
				return -1;
			const auto ref = e.get_entity_reference();
			return ref.classnum == 0 && ref.entnum > 0 && ref.entnum < 4000 ? int(ref.entnum) : -1;
		}
		scripting::entity entity(int n)
		{
			return scripting::entity{game::scr_entref_t{static_cast<unsigned short>(n), 0}};
		}
		scripting::vector script_vec(vec v)
		{
			return {v[0], v[1], v[2]};
		}
		vec vector(const scripting::script_value& v)
		{
			const auto a = v.as<scripting::vector>();
			return {a[0], a[1], a[2]};
		}
		bool tag(int n, const char* name, anchor& out)
		{
			if (n <= 0 || n >= 4000)
				return false;
			try
			{
				const auto e = entity(n);
				const auto p = e.call("gettagorigin", {name}), a = e.call("gettagangles", {name});
				if (!p.is<scripting::vector>() || !a.is<scripting::vector>())
					return false;
				out.position = vector(p);
				const auto angles = vector(a);
				vr::h2::sp::angles_to_quaternion(angles.data(), out.rotation.data());
				return free_climb::finite(out.position) && free_climb::rotation_valid(out.rotation);
			}
			catch (const std::exception&)
			{
				return false;
			}
		}
		int parent()
		{
			const auto* link = read<const void*>(&game::g_entities[0], 0x208);
			const auto* p = link ? read<const game::gentity_s*>(link, 0) : nullptr;
			if (!p)
				return -1;
			const auto delta = reinterpret_cast<std::uintptr_t>(p) -
			                   reinterpret_cast<std::uintptr_t>(game::g_entities.get());
			return delta % sizeof(game::gentity_s) == 0 && delta / sizeof(game::gentity_s) < 4000
			           ? int(delta / sizeof(game::gentity_s))
			           : -1;
		}
		bool fresh(clock::time_point at)
		{
			const auto now = clock::now();
			return at != clock::time_point{} && now >= at && now - at <= 150ms;
		}
		bool input_frame(controller_input::frame& input,
		                 head_pose_bridge::spatial_frame& spatial,
		                 std::array<hand_sample, 2>& hands)
		{
			input = controller_input::latest_interaction();
			const auto* pause = game::Dvar_FindVar("cl_paused");
			if (!pause || pause->current.integer || *game::keyCatchers || !input.focused ||
			    input.orientation_settling || !fresh(input.sampled_at) ||
			    !head_pose_bridge::get_spatial_frame(spatial) || !fresh(spatial.captured_at) ||
			    spatial.generation != input.reference_generation || !std::isfinite(spatial.units_per_meter) ||
			    spatial.units_per_meter < .01f || spatial.units_per_meter > 10000)
				return false;
			position_offsets offsets;
			const auto setting = [](const char* key, float fallback)
			{
				const auto* d = game::Dvar_FindVar(key);
				return d ? d->current.value : fallback;
			};
			offsets = {setting(vr::settings::active_hand_alignment()[0].name, offsets.inward_meters),
			           setting(vr::settings::active_hand_alignment()[1].name, offsets.back_meters),
			           setting(vr::settings::active_hand_alignment()[2].name, offsets.up_meters)};
			for (unsigned h = 0; h < 2; ++h)
				hands[h].valid = tracked_wrist(input, spatial, {}, h, offsets, hands[h].wrist);
			return true;
		}
		free_climb::contact surface(vec from, vec to, int ignored = -1)
		{
			free_climb::contact out;
			if (!free_climb::finite(from) || !free_climb::finite(to) || length(sub(to, from)) > 256)
				return out;
			const auto hit = scripting::call(
			    "bullettrace",
			    {script_vec(from),
			     script_vec(to),
			     0,
			     ignored > 0 ? scripting::script_value{entity(ignored)} : scripting::script_value{}});
			if (!hit.is<scripting::array>())
				return out;
			const auto fields = hit.as<scripting::array>();
			const auto fraction = fields.get(std::string("fraction")),
			           point = fields.get(std::string("position")),
			           normal = fields.get(std::string("normal"));
			if (!fraction.is<float>() || fraction.as<float>() < 0 || fraction.as<float>() >= 1 ||
			    !point.is<scripting::vector>() || !normal.is<scripting::vector>())
				return out;
			out.point = vector(point);
			out.normal = vector(normal);
			out.valid = free_climb::finite(out.point) && free_climb::finite(out.normal) &&
			            length(out.normal) > .9f && out.normal[2] > .65f;
			if (out.valid)
				out.normal = unit(out.normal);
			return out;
		}
		void reset_interaction()
		{
			pull.reset();
			throwing.reset();
			pending_work = 0;
			grasp_enabled = work_enabled = throw_session = false;
			interaction_body = interaction_knife = -1;
			interaction_key = {};
		}
		void reset()
		{
			++generation;
			reset_interaction();
			crawl = {};
			last_sequence = last_reference = last_continuity = 0;
			last_time = 0;
			impaled = false;
			{
				const std::lock_guard lock(pub_mutex);
				published = {};
				ground = {};
			}
		}
		bool valid_pair(int body, int knife)
		{
			if (!eligible() || body <= 0 || knife <= 0)
				return false;
			const scripting::entity level{*game::levelEntityId};
			return body == id(level.get("player_rig")) && knife == id(level.get("knife"));
		}
		bool begin_pair()
		{
			const int body = id(argument(0)), knife = id(argument(1));
			if (!valid_pair(body, knife))
				return false;
			const auto key = weapons::native_carry::entity_key(body);
			if (interaction_body != body || interaction_knife != knife || interaction_key != key)
			{
				reset_interaction();
				interaction_body = body;
				interaction_knife = knife;
				interaction_key = key;
				++generation;
			}
			return true;
		}
		bool looking(int target,
		             const head_pose_bridge::spatial_frame& spatial,
		             float* dot_out = nullptr,
		             vec* direction = nullptr)
		{
			anchor at;
			if (!tag(target, "tag_eye", at))
				return false;
			const auto delta = sub(at.position, spatial.head_position);
			if (length(delta) < 1)
				return false;
			const auto forward = unit(delta);
			const float facing = dot(spatial.head_forward, forward);
			if (dot_out)
				*dot_out = facing;
			if (direction)
				*direction = forward;
			return std::isfinite(facing) && facing >= throw_gaze_dot;
		}
		void install_hooks()
		{
			if (hooks_ready || !map_supported())
				return;
			try
			{
				struct hook
				{
					const char* file;
					const char* function;
					const char* replacement;
				};
				constexpr hook hooks[]{{"maps/af_chase_knife_fight", "_id_CB2E", "crawl"},
				                       {"maps/af_chase_knife_fight_code", "_id_BC66", "knife_grasp"},
				                       {"maps/af_chase_knife_fight_code", "_id_AABC", "knife_grasp"},
				                       {"maps/af_chase_knife_fight_code", "_id_CA9B", "pull"},
				                       {"maps/af_chase_knife_fight_code", "_id_BE34", "throw_wait"},
				                       {"maps/af_chase_knife_fight_code", "_id_AF45", "gaze"},
				                       {"maps/af_chase_anim", "_id_CFD3", "knife_blood"},
				                       {"maps/af_chase_knife_fight_code", "_id_C1BA", "knife_hint"}};
				std::array<std::pair<const char*, const char*>, std::size(hooks)> addresses;
				for (unsigned i = 0; i < addresses.size(); ++i)
					addresses[i] = {
					    scripting::get_function_pos(hooks[i].file, hooks[i].function),
					    scripting::get_function_pos(std::string(script_name), hooks[i].replacement)};
				for (const auto& [from, to] : addresses)
					notifies::set_gsc_hook(from, to, eligible);
				hooks_ready = true;
				registration = "scoped crawl, grasp, pull and throw bridges ready";
			}
			catch (const std::exception& e)
			{
				registration = e.what();
			}
		}
		bool revolver_center(int actor, vec& out)
		{
			if (!ground_ready || actor <= 0 || actor >= 4000)
				return false;
			const auto* object =
			    reinterpret_cast<native_object*>(vr::h2::sp::server_entity_dobj(&game::g_entities[actor]));
			if (!object || !object->models || object->model_count != 1)
				return false;
			const auto* model = object->models[0];
			if (!model || !model->name || std::string_view(model->name) != "weapon_colt_anaconda_animated" ||
			    !model->baseMat || !model->boneNames || !model->numBones)
				return false;
			const auto* bone = game::SL_ConvertToString(model->boneNames[0]);
			if (!bone || std::string_view(bone) != "tag_weapon")
				return false;
			const vec center{model->bounds.midPoint[0], model->bounds.midPoint[1], model->bounds.midPoint[2]};
			if (!free_climb::finite(center) || length(center) > 64)
				return false;
			const auto& b = model->baseMat[0];
			const anchor bind{{b.trans[0], b.trans[1], b.trans[2]},
			                  {b.quat[0], b.quat[1], b.quat[2], b.quat[3]}};
			anchor root;
			if (!tag(actor, "tag_weapon", root) || !free_climb::rotation_valid(bind.rotation))
				return false;
			out = compose(rigid_delta(root, bind), {center, {0, 0, 0, 1}}).position;
			return free_climb::finite(out);
		}
		void crawl_tick(const controller_input::frame& input,
		                const head_pose_bridge::spatial_frame& spatial,
		                const std::array<hand_sample, 2>& raw)
		{
			if (crawl.carrier <= 0 || crawl.done)
				return;
			if (parent() != crawl.carrier ||
			    weapons::native_carry::entity_key(crawl.carrier) != crawl.carrier_key ||
			    weapons::native_carry::entity_key(crawl.body) != crawl.body_key)
			{
				crawl = {};
				return;
			}
			if (!raw[0].valid && !raw[1].valid)
			{
				crawl.solver.rebase();
				crawl.grip_edges = {};
				crawl.sequence = 0;
				crawl.arrival_distance = -1;
				return;
			}
			const int time = game::CG_GetGameTime(0);
			if (time < crawl.time)
			{
				crawl.solver.reset();
				crawl.sequence = 0;
			}
			if (time <= crawl.time || crawl.sequence == input.sequence)
				return;
			const float dt = crawl.time ? float(time - crawl.time) * .001f : .016f;
			crawl.time = time;
			if (input.reference_generation != crawl.reference ||
			    input.continuity_generation != crawl.continuity)
			{
				crawl.solver.rebase();
				crawl.sequence = 0;
				crawl.reference = input.reference_generation;
				crawl.continuity = input.continuity_generation;
			}
			const float units = spatial.units_per_meter;
			if (crawl.units != units)
			{
				if (crawl.units > 0)
					crawl.solver.rescale(crawl.units / units);
				crawl.units = units;
				crawl.sequence = 0;
			}
			auto position = read<vec>(&game::g_entities[crawl.carrier], 0x1c);
			crawl.goal_valid = revolver_center(
			    id(scripting::get_object_variable(*game::levelEntityId, 0xCAC6u)), crawl.goal);
			if (!crawl.height_ready)
			{
				if (!crawl.height_started)
				{
					const auto eye = add(position, crawl.eye_offset);
					const auto floor = surface(add(eye, {0, 0, units}), sub(eye, {0, 0, 2 * units}));
					if (!floor.valid)
					{
						reason = "crawl waiting for native ground";
						return;
					}
					crawl.height_from = position[2];
					crawl.height_to = floor.point[2] + crawl_eye_height_meters * units - crawl.eye_offset[2];
					crawl.height_started = time;
				}
				position[2] =
				    crawl_height_blend(crawl.height_from, crawl.height_to, time - crawl.height_started);
				entity(crawl.carrier).set("origin", script_vec(position));
				crawl.height_ready = time - crawl.height_started >= crawl_settle_milliseconds;
				crawl.solver.rebase();
				crawl.sequence = 0;
				// Preserve the preceding camera pose and settle before granting
				// physical locomotion; never jump upward on its first input.
				return;
			}
			std::array<free_climb::hand, 2> hands{};
			std::array<free_climb::contact, 2> contacts{};
			position_offsets
			    offsets; // Same current calibration for both samples cancels scripted translation/yaw.
			const auto setting = [](const char* n, float d)
			{
				const auto* v = game::Dvar_FindVar(n);
				return v ? v->current.value : d;
			};
			offsets = {setting(vr::settings::active_hand_alignment()[0].name, offsets.inward_meters),
			           setting(vr::settings::active_hand_alignment()[1].name, offsets.back_meters),
			           setting(vr::settings::active_hand_alignment()[2].name, offsets.up_meters)};
			for (unsigned h = 0; h < 2; ++h)
			{
				if (!raw[h].valid)
				{
					crawl.grip_edges[h] = {};
					continue;
				}
				auto& in = hands[h];
				in = {scale(raw[h].wrist.position, 1 / units),
				      scale(raw[h].wrist.position, 1 / units),
				      raw[h].wrist.rotation,
				      true,
				      false};
				crawl.grip_edges[h].consume(input.squeeze[h]);
				in.fixed = crawl.grip_edges[h].armed && input.squeeze[h].active && input.squeeze[h].down;
				if (crawl.sequence)
				{
					anchor previous;
					if (crawl.hands[h].valid &&
					    tracked_wrist(crawl.previous, spatial, {}, h, offsets, previous))
						in.motion = add(crawl.hands[h].motion,
						                scale(sub(raw[h].wrist.position, previous.position), 1 / units));
					const auto point = raw[h].wrist.position;
					auto contact =
					    surface(add(point, {0, 0, .035f * units}), sub(point, {0, 0, .08f * units}));
					if (contact.valid && std::abs(contact.point[2] - point[2]) <= .07f * units)
					{
						contact.point = scale(contact.point, 1 / units);
						contacts[h] = contact;
					}
				}
				in.intent = in.motion;
			}
			auto result =
			    crawl.solver.update(scale(position, 1 / units), crawl.reference, dt, hands, contacts);
			auto goal = result.goal;
			goal[2] = position[2] / units;
			auto desired = scale(free_climb::bounded_step(scale(position, 1 / units), goal, dt, .65f), units);
			desired[2] = position[2];
			const auto destination = crawl.goal_valid ? sub(crawl.goal, crawl.eye_offset) : crawl.finish;
			for (unsigned axis = 0; axis < 2; ++axis)
				desired[axis] = std::clamp(desired[axis],
				                           std::min(crawl.start[axis], destination[axis]) - 48,
				                           std::max(crawl.start[axis], destination[axis]) + 48);
			const auto eye = add(position, crawl.eye_offset), wanted_eye = add(desired, crawl.eye_offset);
			const auto floor = surface(add(wanted_eye, {0, 0, .5f * units}), sub(wanted_eye, {0, 0, units}));
			if (floor.valid)
				desired[2] = floor.point[2] + crawl_eye_height_meters * units - crawl.eye_offset[2];
			else
				desired = position;
			const auto delta = sub(desired, position);
			if (length(delta) > .001f)
			{
				game::Bounds bounds{};
				bounds.halfSize[0] = bounds.halfSize[1] = .16f * units;
				bounds.halfSize[2] = .08f * units;
				const auto collision = free_climb::slide(
				    eye,
				    delta,
				    .005f * units,
				    [&](vec from, vec to)
				    {
					    game::trace_t hit{};
					    game::G_TraceCapsule(&hit, from.data(), to.data(), &bounds, 0, 0x280e831);
					    return free_climb::sweep_hit{hit.fraction,
					                                 {hit.normal[0], hit.normal[1], hit.normal[2]},
					                                 bool(hit.startsolid),
					                                 bool(hit.allsolid)};
				    });
				const auto resolved = sub(collision.position, crawl.eye_offset);
				entity(crawl.carrier).set("origin", script_vec(resolved));
				++moves;
				if (collision.blocked)
					crawl.solver.obstructed(scale(resolved, 1 / units));
			}
			const auto arrival = crawl.goal_valid ? crawl_reach(raw, crawl.goal, units) : crawl_arrival{};
			crawl.arrival_distance = arrival.nearest_meters;
			crawl.done = arrival.hands != 0;
			for (unsigned h = 0; h < 2; ++h)
				if (crawl.solver.hands[h].surface.valid)
				{
					const auto& support = crawl.solver.hands[h];
					crawl.fixed_wrists[h] = {
					    scale(add(support.surface.point,
					              scale(support.surface.normal, .04f + support.withdrawal)),
					          units),
					    support.rotation};
				}
			crawl.previous = input;
			crawl.hands = hands;
			crawl.sequence = input.sequence;
		}
		grounding ground_actor(int actor, int gun, bool active)
		{
			grounding out{actor, gun, 0, generation};
			if (!active || !ground_ready || actor <= 0)
				return out;
			// Native per-bone mesh bounds include the boot sole. Do not guess an
			// ankle-to-ground offset or move the player's camera/scene origin.
			auto* object =
			    reinterpret_cast<native_object*>(vr::h2::sp::server_entity_dobj(&game::g_entities[actor]));
			if (!object || !object->models || !object->model_count || object->model_count > 32)
				return out;
			float gap = 10000;
			unsigned feet{};
			for (unsigned m = 0; m < object->model_count; ++m)
			{
				const auto* model = object->models[m];
				if (!model || !model->name ||
				    !std::string_view(model->name).starts_with("body_vil_shepherd") || !model->boneNames ||
				    !model->boneInfo || model->numBones > 254)
					continue;
				for (unsigned b = 0; b < model->numBones; ++b)
				{
					const auto* name = game::SL_ConvertToString(model->boneNames[b]);
					if (!name ||
					    (std::string_view(name) != "j_ankle_le" && std::string_view(name) != "j_ankle_ri"))
						continue;
					anchor ankle;
					if (!tag(actor, name, ankle))
						continue;
					const auto& box = model->boneInfo[b].bounds;
					vec midpoint{box.midPoint[0], box.midPoint[1], box.midPoint[2]},
					    half{box.halfSize[0], box.halfSize[1], box.halfSize[2]};
					if (!free_climb::finite(midpoint) || !free_climb::finite(half) ||
					    std::any_of(half.begin(), half.end(), [](float x) { return x <= 0 || x > 16; }))
						continue;
					float sole = 10000;
					for (unsigned corner = 0; corner < 8; ++corner)
					{
						auto p = midpoint;
						for (unsigned a = 0; a < 3; ++a)
							p[a] += ((corner >> a) & 1 ? 1 : -1) * half[a];
						sole = std::min(sole, compose(ankle, {p, {0, 0, 0, 1}}).position[2]);
					}
					const auto floor =
					    surface(add(ankle.position, {0, 0, 8}), sub(ankle.position, {0, 0, 96}), actor);
					if (!floor.valid)
						continue;
					gap = std::min(gap, sole - floor.point[2]);
					++feet;
				}
			}
			if (feet == 2 && gap > .05f && gap < 48)
			{
				out.delta = -gap;
				++groundings;
			}
			return out;
		}
		void tick()
		{
			const std::lock_guard lock(state_mutex);
			install_hooks();
			if (!scene_context() || !game::g_entities[0].client)
			{
				reset();
				return;
			}
			bool dead{};
			if (!player_life::read(game::g_entities[0].client, dead) || dead)
			{
				reset();
				return;
			}
			const scripting::entity level{*game::levelEntityId};
			const auto flags_value = level.get("flag"), entry_value = level.get("start_point");
			if (!flags_value.is<scripting::array>() || !entry_value.is<std::string>())
			{
				reset();
				return;
			}
			const auto flags = flags_value.as<scripting::array>();
			const auto entry = entry_value.as<std::string>();
			const auto flag = [&](const char* n)
			{
				const auto v = flags.get(std::string(n));
				return v.is<int>() && v.as<int>() != 0;
			};
			const auto credits = flags.get(std::string("do_museum_credits"));
			if (!credits.is<int>() || credits.as<int>())
			{
				reset();
				return;
			}
			const int time = game::CG_GetGameTime(0);
			if (time < last_time)
				reset();
			last_time = time;
			const auto failed_value = level.get("missionfailed");
			const bool failed =
			    flag("missionfailed") || (failed_value.is<int>() && failed_value.as<int>() != 0);
			presentation p;
			p.active = true;
			p.body = id(level.get("player_rig"));
			p.secondary_body = id(scripting::get_object_variable(*game::levelEntityId, 0xB405u));
			p.knife = id(level.get("knife"));
			p.shepherd = id(scripting::get_object_variable(*game::levelEntityId, 0xB416u));
			p.phase = classify({"credits_1",
			                    entry,
			                    true,
			                    false,
			                    flag("player_standing") && flag("start_doing_aftermath_walk"),
			                    flag("turn_buckle_start"),
			                    impaled.load(),
			                    flag("bloody_player_rig"),
			                    flag("crawl_gameplay_started"),
			                    flag("crawl_gameplay_complete"),
			                    flag("shepherd_fights_price_sequence_start"),
			                    flag("player_uses_knife"),
			                    flag("throw_knife_pulled_out"),
			                    flag("throw_knife_gameplay_started"),
			                    flag("player_throws_knife"),
			                    flag("shepherd_killed"),
			                    grasp_enabled,
			                    failed});
			if (p.phase == stage::none)
			{
				reset();
				return;
			}
			p.independent = configured() && independent(p.phase);
			p.hide_body = p.independent;
			if (p.phase == stage::grounded && !flag("af_chase_final_fight"))
				p.waiting_price = id(level.get("price"));
			p.helper_camera = p.phase == stage::wounded && parent() != p.body;
			p.ground_actor = configured() && p.phase == stage::grounded && !flag("af_chase_final_fight");
			p.carrier = crawl.carrier;
			p.lease = generation;
			controller_input::frame input;
			head_pose_bridge::spatial_frame spatial;
			std::array<hand_sample, 2> hands{};
			const bool tracking = !failed && configured() && input_frame(input, spatial, hands);
			p.reference = input.reference_generation;
			if (!tracking)
			{
				pull.reset();
				throwing.reset();
				pending_work = 0;
				crawl.solver.rebase();
				crawl.sequence = 0;
			}
			else
			{
				if (input.reference_generation != last_reference ||
				    input.continuity_generation != last_continuity)
				{
					pending_work = 0;
					throwing.reset();
					last_reference = input.reference_generation;
					last_continuity = input.continuity_generation;
				}
				if (p.phase == stage::crawl)
					crawl_tick(input, spatial, hands);
				anchor knife_pose;
				if (body_control(p.phase) && tag(p.knife, "tag_knife", knife_pose))
				{
					for (unsigned h = 0; h < 2; ++h)
						p.grips[h] = compose(knife_pose, grips::second[h]);
					const bool admitted = grasp_enabled &&
					                      (p.phase == stage::pull_wait || p.phase == stage::pull) &&
					                      valid_pair(interaction_body, interaction_knife);
					auto physical = hands;
					for (unsigned h = 0; h < 2; ++h)
						if (physical[h].valid)
							physical[h].wrist.rotation = from_axis(input.grip[h].tracking.orientation);
					const auto amount =
					    pull.update(input, physical, p.grips, spatial.units_per_meter, admitted);
					p.held = pull.held();
					p.hand_pose = &grips::second_hand;
					if (!p.held)
						pending_work = 0;
					else if (work_enabled)
						pending_work = std::min(.2f, pending_work + amount);
				}
				if (p.phase == stage::throw_ready && throw_session)
				{
					float gaze{};
					vec toward{};
					head_pose_bridge::tracking_reference reference_frame;
					vec local_hand{}, local_target{};
					const bool aiming =
					    hands[1].valid && looking(p.shepherd, spatial, &gaze, &toward) &&
					    head_pose_bridge::get_tracking_reference(reference_frame) &&
					    reference_frame.generation == input.reference_generation &&
					    head_pose_bridge::tracking_position(
					        reference_frame, input.grip[1].tracking.position_meters, local_hand);
					if (aiming)
					{
						for (unsigned axis = 0; axis < 3; ++axis)
							local_target[axis] = dot(toward, spatial.world_yaw_axis[axis]);
						if (throwing.update(local_hand,
						                    reference_frame.head_position_meters,
						                    local_target,
						                    gaze,
						                    input.reference_generation,
						                    time,
						                    true))
							++throws;
					}
					else
						throwing.reset();
					p.hand_pose = &grips::throwing_hand;
					p.held = 0;
				}
			}
			if (p.phase != stage::pull_wait && p.phase != stage::pull && p.phase != stage::wounded)
			{
				grasp_enabled = work_enabled = false;
				pending_work = 0;
			}
			if (p.phase != stage::throw_ready)
			{
				throw_session = false;
				throwing.reset();
			}
			p.throwing_blade = p.phase == stage::throw_ready && throw_session;
			if (p.throwing_blade)
			{
				p.knife_attachment = knife_in_wrist;
				p.hand_pose = &grips::throwing_hand;
			}
			if (crawl.carrier > 0 && crawl.reference == p.reference && p.phase == stage::crawl)
			{
				p.crawl_held = crawl.solver.held();
				p.crawl_wrists = crawl.fixed_wrists;
			}
			const auto correction =
			    ground_actor(p.shepherd,
			                 id(scripting::get_object_variable(*game::levelEntityId, 0xCAC6u)),
			                 p.ground_actor);
			{
				const std::lock_guard pub(pub_mutex);
				published = p;
				ground = correction;
			}
			reason = tracking ? name(p.phase) : "native scene retained; tracking suspended";
			last_sequence = input.sequence;
		}
		bool grasp_ready()
		{
			const std::lock_guard lock(state_mutex);
			if (!begin_pair())
				return false;
			grasp_enabled = true;
			controller_input::frame input;
			head_pose_bridge::spatial_frame spatial;
			std::array<hand_sample, 2> hands{};
			if (!input_frame(input, spatial, hands))
				return false;
			for (unsigned h = 0; h < 2; ++h)
				if ((pull.held() & (1u << h)) && hands[h].valid && input.squeeze[h].active &&
				    input.squeeze[h].down)
					return true;
			return false;
		}
		float pull_work()
		{
			const std::lock_guard lock(state_mutex);
			if (!begin_pair() || !work_enabled || !pull.held())
				return 0;
			controller_input::frame input;
			head_pose_bridge::spatial_frame spatial;
			std::array<hand_sample, 2> hands{};
			bool held = false;
			if (input_frame(input, spatial, hands))
				for (unsigned h = 0; h < 2; ++h)
					held |= (pull.held() & (1u << h)) && hands[h].valid && input.squeeze[h].active &&
					        input.squeeze[h].down;
			if (!held)
			{
				pending_work = 0;
				return 0;
			}
			const float value = std::exchange(pending_work, 0.f);
			if (value > 0)
				++pulls;
			return value;
		}
		std::uint64_t delivered_throw{};
		bool prepare_throw_pair()
		{
			if (!throw_session)
			{
				anchor wrist, knife;
				if (!tag(interaction_body, "j_wrist_ri", wrist) ||
				    !tag(interaction_knife, "tag_knife", knife))
					return false;
				const auto attachment = compose(inverse(wrist), knife);
				if (length(attachment.position) > 12)
				{
					++throw_attachment_rejections;
					reason = "throw knife/wrist attachment is outside its native grip";
					return false;
				}
				knife_in_wrist = attachment;
				throwing.reset();
				throw_session = true;
				delivered_throw = throws;
				++throw_preparations;
			}
			return true;
		}
		bool throw_ready()
		{
			const std::lock_guard lock(state_mutex);
			if (!begin_pair() || !prepare_throw_pair())
				return false;
			if (throws == delivered_throw)
				return false;
			controller_input::frame input;
			head_pose_bridge::spatial_frame spatial;
			std::array<hand_sample, 2> hands{};
			if (!input_frame(input, spatial, hands) || !hands[1].valid || !looking(id(argument(2)), spatial))
			{
				delivered_throw = throws;
				throwing.reset();
				return false;
			}
			return true;
		}
		bool crawl_state()
		{
			const std::lock_guard lock(state_mutex);
			if (!eligible())
				return false;
			const int body = id(argument(0)), carrier = id(argument(1));
			const auto a = argument(2), b = argument(3);
			const scripting::entity level{*game::levelEntityId};
			if (body <= 0 || carrier <= 0 || body != id(level.get("player_rig")) ||
			    !a.is<scripting::vector>() || !b.is<scripting::vector>())
				return false;
			const auto start = vector(a), finish = vector(b);
			const auto owner = entity(carrier).get("animname");
			if (!owner.is<std::string>() || owner.as<std::string>() != "vr_ending_carrier" ||
			    parent() != carrier || !free_climb::finite(start) || !free_climb::finite(finish) ||
			    length(sub(start, finish)) > 1000)
				return false;
			const auto key = weapons::native_carry::entity_key(carrier);
			if (crawl.carrier != carrier || crawl.carrier_key != key)
			{
				const auto eye = entity(0).call("geteye");
				if (!eye.is<scripting::vector>())
					return false;
				const auto offset = sub(vector(eye), read<vec>(&game::g_entities[carrier], 0x1c));
				if (!free_climb::finite(offset) || length(offset) > 128)
					return false;
				crawl = {};
				crawl.body = body;
				crawl.carrier = carrier;
				crawl.carrier_key = key;
				crawl.body_key = weapons::native_carry::entity_key(body);
				crawl.start = start;
				crawl.finish = finish;
				crawl.eye_offset = offset;
				++generation;
			}
			return crawl.done;
		}
		int ground_slot(const void* object, grounding& value)
		{
			if (!object || !game::CL_IsCgameInitialized() ||
			    sequences::for_player(game::CG_GetPredictedPlayerState(0)).scene !=
			        sequences::scenario::ending)
				return -1;
			{
				const std::lock_guard lock(pub_mutex);
				value = ground;
			}
			if (value.delta == 0)
				return -1;
			for (unsigned i = 0; i < 2; ++i)
			{
				const int n = i ? value.gun : value.actor;
				if (n > 0 && n < 4000 && vr::h2::sp::client_entity_dobj(n, 0) == object)
					return int(i);
			}
			return -1;
		}
	}
	presentation latest() noexcept
	{
		const std::lock_guard lock(pub_mutex);
		return published;
	}
	void update()
	{
		try
		{
			tick();
		}
		catch (const std::exception& e)
		{
			const std::lock_guard lock(state_mutex);
			++faults;
			pending_work = 0;
			pull.reset();
			throwing.reset();
			reason = e.what();
			const std::lock_guard pub(pub_mutex);
			published.held = published.crawl_held = 0;
			published.throwing_blade = false;
			published.hand_pose = nullptr;
			ground = {};
		}
	}
	void constrain_hands(std::array<anchor, 2>& targets,
	                     const std::array<quat, 2>&,
	                     vec offset,
	                     std::uint64_t reference) noexcept
	{
		const auto p = latest();
		if (p.phase != stage::crawl || p.reference != reference)
			return;
		for (unsigned h = 0; h < 2; ++h)
			if (p.crawl_held & (1u << h))
				targets[h] = {sub(p.crawl_wrists[h].position, offset), p.crawl_wrists[h].rotation};
	}
	bool owns_pose(const void* object) noexcept
	{
		grounding v;
		return ground_slot(object, v) >= 0;
	}
	bool wants_pose(const void* pointer, const unsigned* requested) noexcept
	{
		if (!requested)
			return false;
		const auto* object = static_cast<const native_object*>(pointer);
		unsigned count{};
		for (unsigned b = 0; b < object->bone_count; ++b)
			count += bool(requested[b / 32] & (0x80000000u >> (b % 32)));
		return count > 1; // Tag-only queries must not pre-complete native controllers.
	}
	void apply_pose(void* pointer, bool rebuilt) noexcept
	{
		grounding v;
		const int slot = ground_slot(pointer, v);
		if (slot < 0)
			return;
		auto& object = *static_cast<native_object*>(pointer);
		const std::lock_guard lock(pose_mutex);
		if (grounded_objects[slot] != pointer)
		{
			grounded_objects[slot] = pointer;
			grounded_poses[slot] = {};
		}
		if (!grounded_poses[slot].begin({reinterpret_cast<std::uintptr_t>(pointer),
		                                 reinterpret_cast<std::uintptr_t>(object.matrices),
		                                 object.timestamp},
		                                rebuilt))
			return;
		if (!object.matrices || !object.bone_count || object.bone_count > 254 || !std::isfinite(v.delta) ||
		    v.delta > 0 || v.delta < -48)
			return;
		for (unsigned b = 0; b < object.bone_count; ++b)
			if (!finite_twist_pose(object.matrices[b]))
				return;
		for (unsigned b = 0; b < object.bone_count; ++b)
			object.matrices[b].position[2] += v.delta;
	}
	std::string status()
	{
		const std::lock_guard lock(state_mutex);
		return std::format(
		           "ending={} hooks={} registration={} held={} pull_updates={} throws={} crawl_moves={} grounding_updates={} faults={} reason={}\n",
		           name(latest().phase),
		           hooks_ready,
		           registration,
		           pull.held(),
		           pulls,
		           throws,
		           moves,
		           groundings,
		           faults,
		           reason) +
		       std::format("throw_prepared={} attachment_rejections={} session={}\n",
		                   throw_preparations,
		                   throw_attachment_rejections,
		                   throw_session) +
		       std::format(
		           "crawl_carrier={} height_ready={} goal_valid={} done={} eye_offset=({},{},{}) revolver=({},{},{}) arrival_distance_m={}\n",
		           crawl.carrier,
		           crawl.height_ready,
		           crawl.goal_valid,
		           crawl.done,
		           crawl.eye_offset[0],
		           crawl.eye_offset[1],
		           crawl.eye_offset[2],
		           crawl.goal[0],
		           crawl.goal[1],
		           crawl.goal[2],
		           crawl.arrival_distance);
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
			constexpr std::uint8_t model_bytes[]{0x48, 0x8d, 0x05, 0xc9, 0x6e, 0xd3, 0x04, 0x48, 0x2b, 0xc8};
			std::array<std::uint8_t, sizeof(model_bytes)> model_mask{};
			model_mask.fill(255);
			ground_ready = bool(utils::hook_validation::verify_masked_bytes(
			    reinterpret_cast<void*>(vr::h2::sp::server_entity_dobj.address()),
			    {model_bytes, model_mask.data(), sizeof(model_bytes)}));
			gsc::register_virtual_source(std::string(script_name), std::string(script_source), "ending");
			gsc::add_function("vrendingknifehint",
			                  []
			                  {
				                  constexpr auto key = game_text::key::ending_knife_pull;
				                  const auto message = hud_prompts::compose(
				                      key, game_text::effective_locale(key, game_text::current()));
				                  const auto text =
				                      message ? hud_prompts::text(*message, hud_prompts::style::native_colors)
				                              : std::string{};
				                  game::Scr_AddString(text.c_str());
			                  });
			gsc::add_function("vrendinggrasp", [] { game::Scr_AddInt(grasp_ready()); });
			gsc::add_function("vrendingpullbegin",
			                  []
			                  {
				                  const std::lock_guard lock(state_mutex);
				                  if (begin_pair())
				                  {
					                  grasp_enabled = work_enabled = true;
					                  pending_work = 0;
				                  }
			                  });
			gsc::add_function("vrendingpullwork",
			                  []
			                  {
				                  const auto work = pull_work();
				                  game::Scr_ClearOutParams();
				                  scripting::push_value(work);
			                  });
			gsc::add_function("vrendingpullend",
			                  []
			                  {
				                  const std::lock_guard lock(state_mutex);
				                  work_enabled = false;
				                  pending_work = 0;
			                  });
			gsc::add_function("vrendingthrow", [] { game::Scr_AddInt(throw_ready()); });
			gsc::add_function("vrendingthrowprepare",
			                  []
			                  {
				                  const std::lock_guard lock(state_mutex);
				                  game::Scr_AddInt(begin_pair() && prepare_throw_pair());
			                  });
			gsc::add_function("vrendingthrowend",
			                  []
			                  {
				                  const std::lock_guard lock(state_mutex);
				                  throw_session = false;
				                  const std::lock_guard pub(pub_mutex);
				                  published.throwing_blade = false;
				                  if (published.phase == stage::throw_ready)
					                  published.phase = stage::throwing;
			                  });
			gsc::add_function("vrendinggaze",
			                  []
			                  {
				                  controller_input::frame input;
				                  head_pose_bridge::spatial_frame spatial;
				                  std::array<hand_sample, 2> hands{};
				                  game::Scr_AddInt(eligible() && input_frame(input, spatial, hands) &&
				                                   looking(id(argument(0)), spatial));
			                  });
			gsc::add_function("vrendingcrawl", [] { game::Scr_AddInt(crawl_state()); });
			gsc::add_function("vrendingcrawlend",
			                  []
			                  {
				                  const std::lock_guard lock(state_mutex);
				                  crawl = {};
			                  });
			scripting::on_notify_alias(
			    [](unsigned owner, game::scr_string_t event, const game::VariableValue*) -> game::scr_string_t
			    {
				    if (owner != *game::levelEntityId || !map_supported() || !latest().active)
					    return 0;
				    const auto* name = game::SL_ConvertToString(event);
				    if (name && std::string_view(name) == "knife_in_player")
					    impaled = true;
				    return 0;
			    });
			scripting::on_level_start(
			    []
			    {
				    const std::lock_guard lock(state_mutex);
				    reset();
				    hooks_ready = false;
				    install_hooks();
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
			command::add("vr_ending_status",
			             []
			             {
				             const auto text = status();
				             console::print_text(console::con_type_info, text);
				             utils::io::write_file_atomic("minidumps/overlord-ending.txt", text);
			             });
		}
		void pre_destroy() override
		{
			alive = false;
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::ending::component)
