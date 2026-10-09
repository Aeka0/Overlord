#include <std_include.hpp>
#include "../../../h2/entrypoints.hpp"
#include "runtime.hpp"
#include "model.hpp"
#include "physical.hpp"
#include "pickaxe_policy.hpp"
#include "../../official_cheats.hpp"
#include "../../native_weapon_read.hpp"
#include "../../hands/position_offset.hpp"
#include "../../abdominal_interaction.hpp"
#include "../../hands/attachment_pose.hpp"
#include "../../hand_interaction/runtime.hpp"
#include "../../native_scripted_control.hpp"
#include "../../native_carry.hpp"
#include "../sequences/oilrig.hpp"
#include "../../weapon_feedback.hpp"
#include "../../../engine_stereo_view.hpp"
#include "component/scene_models.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/scripting/execution.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::equipment::special::cliffhanger
{
	namespace
	{
		using namespace hands;
		using namespace hands::pose_math;
		namespace hi = hand_interaction;
		using clock = controller_input::clock;
		struct snapshot
		{
			context mission;
			std::shared_ptr<model> asset;
			vr::hand holder{vr::hand::none};
			std::uint64_t revision{}, reference{};
			std::array<quat, 2> basis{}, mirror{};
			std::array<anchor, 2> wrists{};
			unsigned tracked{};
			bool hands_ready{};
			waist_pickaxes tools{};
			std::array<vec, 2> waist_centers{}; // metres in the calibrated slot frame
			bool waist_ready{};
			clock::time_point at{};
		};
		std::mutex mutex;
		snapshot published, submitted;
		context mission;
		std::shared_ptr<model> asset;
		game::XModel* attempted{};
		std::atomic_bool assets_ready{};
		std::atomic_uint64_t generation{1}, takes{}, returns{}, native_detonations{}, failures{};
		std::atomic_int detonated_at{-1};
		std::atomic<const char*> reason{"waiting for Cliffhanger"};
		vr::hand holder{vr::hand::none};
		std::uint64_t revision{1}, reference{}, epoch{};
		int last_time{};
		grab_intent intention;
		trigger_gate trigger;
		waist_pickaxes tools;
		grab_intent pick_intention;
		std::atomic_uint32_t pick_weapon{};
		std::atomic_uint64_t pick_takes{}, pick_returns{};
		std::uint64_t pick_continuity{};
		bool oilrig_selected{};
		std::array<unsigned short, 3> lighting{};
		bool fresh(clock::time_point t)
		{
			const auto now = clock::now();
			return now >= t && now - t <= 150ms;
		}
		bool map_supported()
		{
			const auto* d = game::Dvar_FindVar("mapname");
			return d && d->current.string &&
			       (std::string_view(d->current.string) == "cliffhanger" || sequences::oilrig::supported());
		}
		bool enabled()
		{
			const auto* d = game::Dvar_FindVar("vr_independentHands");
			return d && d->current.enabled && head_pose_bridge::get_status().enabled;
		}
		bool abdominal_enabled()
		{
			const auto* d = game::Dvar_FindVar("vr_abdominalEquipment");
			return d && d->current.enabled;
		}
		std::array<char, 64> weapon_name(unsigned token)
		{
			std::array<char, 64> text{};
			const auto value = weapons::native_weapon_read::get(token);
			if (value)
				std::copy_n(value.name.begin(), text.size() - 1, text.begin());
			return text;
		}
		template <class T> T field(const void* p, unsigned offset)
		{
			T v{};
			if (p)
				std::memcpy(&v, static_cast<const std::byte*>(p) + offset, sizeof(v));
			return v;
		}
		void stow()
		{
			if (vr::valid_hand(holder))
			{
				holder = vr::hand::none;
				++revision;
				++returns;
			}
			intention.reset();
		}
		void stow_picks()
		{
			for (unsigned i = 0; i < 2; ++i)
				if (tools.stow(i))
					++pick_returns;
			pick_intention.reset();
		}
		void restore_oilrig_selection()
		{
			if (!oilrig_selected || vr::valid_hand(holder) ||
			    !scheduler::is_executing(scheduler::pipeline::server) || !sequences::oilrig::supported())
				return;
			const auto* ps = game::g_entities[0].client;
			if (!ps || !scripted_control::allowed(ps))
				return;
			if ((field<unsigned>(ps, 0x3bc) & 511) != mission.weapon ||
			    weapons::native_carry::select(weapons::carry::current_hold().weapon))
				oilrig_selected = false;
		}
		void publish(const hi::frame* f = nullptr)
		{
			const std::lock_guard lock(mutex);
			published.mission = mission;
			published.holder = holder;
			published.revision = revision;
			published.tools = tools;
			if (f)
			{
				published.reference = f->input.reference_generation;
				published.at = f->input.sampled_at;
				published.wrists = f->wrists;
				published.tracked = f->valid_hands;
				published.waist_ready = f->holsters.valid;
				if (f->holsters.valid)
					for (unsigned i = 0; i < 2; ++i)
						published.waist_centers[i] =
						    body_local(head_pose_bridge::body_slots_frame(f->body), f->holsters.centers[i]);
			}
		}
		anchor pick_attachment(unsigned side, unsigned h, const snapshot& s)
		{
			const auto a = side ? authored::pick_r_attachment : authored::pick_l_attachment;
			return side == h ? a : pose_mirror::object_in_wrist({}, a, s.mirror[h]);
		}
		anchor waist_root(const head_pose_bridge::spatial_frame& body, const snapshot& s, unsigned side)
		{
			weapons::carry::holsters slots;
			const auto frame = head_pose_bridge::body_slots_frame(body);
			for (unsigned i = 0; i < 2; ++i)
				slots.centers[i] = body_world(frame, s.waist_centers[i]);
			return pickaxe_stowed(body, slots, side);
		}
		anchor attachment(unsigned h, const snapshot& s, bool pick)
		{
			if (pick)
				return h ? authored::pick_r_attachment : authored::pick_l_attachment;
			return h ? authored::detonator_attachment
			         : hands::pose_mirror::object_in_wrist({}, authored::detonator_attachment, s.mirror[h]);
		}
		anchor root(unsigned h, const snapshot& s, bool pick)
		{
			auto wrist = s.wrists[h];
			wrist.rotation = normalize(multiply(wrist.rotation, s.basis[h]));
			return compose(wrist, attachment(h, s, pick));
		}
		hi::target target(unsigned part = 0)
		{
			return hi::object(
			    hi::domain::special, {mission.weapon, part ? mission.generation : revision}, 30 + part);
		}
		hi::grasp pick_grasp(unsigned side, bool next = false)
		{
			return {hi::object(hi::domain::special, {pick_weapon.load(), tools.lease(side, next)}, 40 + side),
			        hi::role::control,
			        hi::button::grip,
			        hi::recipe::single,
			        hi::capability::melee};
		}
		bool visible(const snapshot& s, unsigned part)
		{
			if (!s.mission.active || s.mission.native_body || !s.asset || !fresh(s.at))
				return false;
			if (part)
			{
				if (s.mission.picks)
					return (s.tracked & (1u << (part - 1))) != 0;
				if (!s.mission.waist_picks || !cheats::waist_pickaxes() || cheats::transitioning() ||
				    !s.waist_ready || !pick_weapon || !scripted_control::predicted_allowed() ||
				    ::vr::gameplay::cliffhanger_physical::independent_hands())
					return false;
				const auto h = s.tools.holders[part - 1];
				return vr::valid_hand(h) ? (s.tracked & (1u << unsigned(h))) != 0
				                         : sequences::chest_equipment_visible();
			}
			if (!s.mission.detonator)
				return false;
			return !s.mission.consumed || (vr::valid_hand(s.holder) && s.mission.detonated_at >= 0 &&
			                               s.mission.time - s.mission.detonated_at <= 850);
		}
		void collect_picks(const hi::frame& f, const snapshot& s)
		{
			if (!s.mission.waist_picks || !s.asset || !s.hands_ready || !pick_weapon || !f.holsters.valid ||
			    !cheats::waist_pickaxes() || cheats::transitioning() || !f.input.focused ||
			    f.input.orientation_settling || !sequences::chest_equipment_visible())
			{
				stow_picks();
				return;
			}
			unsigned available{}, pressed{}, released{};
			for (unsigned h = 0; h < 2; ++h)
			{
				const auto edge = hi::input(vr::hand(h), hi::button::grip);
				if (hi::free(vr::hand(h)))
					available |= 1u << h;
				if (edge.press)
					pressed |= 1u << h;
				if (edge.release)
					released |= 1u << h;
			}
			const auto pending =
			    pick_intention.consume(f.input, available & f.valid_hands, pressed, released);
			for (unsigned h = 0; h < 2; ++h)
				if (pending & (1u << h))
				{
					const auto edge = hi::input(vr::hand(h), hi::button::grip);
					if (!edge.down)
						continue;
					for (unsigned side = 0; side < 2; ++side)
						if (!vr::valid_hand(tools.holders[side]) &&
						    f.holsters.volumes[side].contains(f.wrists[h].position))
						{
							const float distance =
							    length(sub(f.wrists[h].position, f.holsters.centers[side])) /
							    f.holsters.radii[side];
							hi::offer({vr::hand(h),
							           pick_grasp(side, true),
							           edge.event,
							           30,
							           distance,
							           1,
							           true,
							           true});
						}
				}
		}
		void update_picks(const hi::frame& f)
		{
			if (!mission.waist_picks || !cheats::waist_pickaxes() || cheats::transitioning())
			{
				stow_picks();
				return;
			}
			for (unsigned side = 0; side < 2; ++side)
			{
				if (const auto h = tools.holders[side]; vr::valid_hand(h))
				{
					const auto edge = hi::input(h, hi::button::grip);
					const auto grasp = pick_grasp(side);
					if (!(f.valid_hands & (1u << unsigned(h))) || edge.release || !edge.down ||
					    hi::pose(h).driver != grasp.destination || !hi::allows_melee(h))
						if (tools.stow(side))
							++pick_returns;
				}
				if (vr::valid_hand(tools.holders[side]))
					continue;
				for (unsigned h = 0; h < 2; ++h)
					if (hi::granted(vr::hand(h),
					                hi::domain::special,
					                pick_grasp(side, true).destination.object,
					                hi::button::grip,
					                hi::role::control) &&
					    (f.valid_hands & (1u << h)) && hi::input(vr::hand(h), hi::button::grip).down &&
					    tools.take(side, vr::hand(h)))
					{
						++pick_takes;
						weapons::feedback::carry_confirmation(vr::hand(h), f.input);
						break;
					}
			}
		}
		void refresh()
		{
			const auto c = latest();
			if (!c.active || !scene_models::ready() || !game::CL_IsCgameInitialized())
				return;
			if (!c.oilrig && !pick_weapon)
				pick_weapon = weapons::native_weapon_read::find("h2_cheatpickaxe");
			if (asset)
				return;
			auto* c4 = game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, "h2_viewmodel_c4", 0).model;
			if (!c4 || c4 == attempted)
				return;
			attempted = c4;
			std::array<game::XModel*, 2> picks{};
			if (!c.oilrig)
				picks = {
				    game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, "viewmodel_ice_picker_03", 0).model,
				    game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, "viewmodel_ice_picker", 0).model};
			auto next = std::make_shared<model>();
			if (!next->create(c4, picks, !c.oilrig))
			{
				reason = "native mission prop geometry rejected";
				++failures;
				return;
			}
			asset = next;
			{
				const std::lock_guard lock(mutex);
				published.asset = next;
			}
			assets_ready = true;
			reason = "native mission props ready";
		}
		void submit()
		{
			snapshot s;
			{
				const std::lock_guard lock(mutex);
				s = published;
				submitted = s;
			}
			if (!enabled() || !game::CL_IsCgameInitialized() ||
			    player_life::dead(game::CG_GetPredictedPlayerState(0)))
				return;
			for (unsigned part = 0; part < 3; ++part)
				if (visible(s, part))
				{
					anchor at;
					game::XModel* mesh{};
					if (part)
					{
						const auto side = part - 1;
						mesh = s.asset->picks[side].model();
						if (s.mission.picks)
							at = root(side, s, true);
						else if (const auto h = s.tools.holders[side]; vr::valid_hand(h))
						{
							auto wrist = s.wrists[unsigned(h)];
							wrist.rotation = normalize(multiply(wrist.rotation, s.basis[unsigned(h)]));
							at = compose(wrist, pick_attachment(side, unsigned(h), s));
						}
						else
						{
							head_pose_bridge::spatial_frame body;
							if (!head_pose_bridge::get_spatial_frame(body) || body.generation != s.reference)
								continue;
							at = waist_root(body, s, side);
						}
					}
					else
					{
						if (vr::valid_hand(s.holder))
							at = root(unsigned(s.holder), s, false);
						else
						{
							head_pose_bridge::spatial_frame body;
							if (!head_pose_bridge::get_spatial_frame(body) || body.generation != s.reference)
								continue;
							at = detonator_stowed(body, s.asset->center);
						}
						mesh =
						    s.asset->detonator(s.mission.detonated_at < 0
						                           ? -1.f
						                           : float(s.mission.time - s.mission.detonated_at) * .001f);
					}
					game::GfxScaledPlacement p{};
					p.scale = 1;
					std::copy(at.position.begin(), at.position.end(), p.base.origin);
					std::copy(at.rotation.begin(), at.rotation.end(), p.base.quat);
					float white[]{1, 1, 1, 1};
					scene_models::submit(
					    mesh, &p, scene_models::no_cast_shadow, &lighting[part], white, white, white, 8.f);
				}
		}
		scene_models::placement_result prepare(const scene_models::preparation& p,
		                                       const void* entry,
		                                       game::GfxPlacement& current,
		                                       game::GfxPlacement& previous) noexcept
		{
			using result = scene_models::placement_result;
			std::uintptr_t handle{};
			if (!utils::native_memory::read_bytes(&handle, static_cast<const std::byte*>(entry) + 0x68, 8))
				return result::unchanged;
			unsigned part = 3;
			for (unsigned i = 0; i < 3; ++i)
				if (handle == reinterpret_cast<std::uintptr_t>(&lighting[i]))
					part = i;
			if (part == 3)
				return result::unchanged;
			snapshot s, now;
			{
				const std::lock_guard lock(mutex);
				s = submitted;
				now = published;
			}
			if (!visible(s, part) || !visible(now, part) || s.reference != now.reference ||
			    s.mission.generation != now.mission.generation || (!part && s.revision != now.revision))
				return result::omit;
			if (part &&
			    (s.mission.picks != now.mission.picks || s.mission.waist_picks != now.mission.waist_picks ||
			     s.tools.revisions[part - 1] != now.tools.revisions[part - 1] ||
			     s.tools.holders[part - 1] != now.tools.holders[part - 1]))
				return result::omit;
			std::array<float, 12> camera{};
			vec origin{};
			attachments::solved pose;
			if (!utils::native_memory::read_bytes(camera.data(),
			                                      static_cast<const std::byte*>(p.record) +
			                                          engine_stereo_view::h2_view_origin_offset,
			                                      sizeof(camera)) ||
			    !attachments::for_record(p.record, camera, pose) || pose.reference != s.reference ||
			    !utils::native_memory::read_bytes(
			        origin.data(),
			        static_cast<const std::byte*>(p.record) +
			            engine_stereo_view::h2_current_model_placement_origin_offset,
			        sizeof(origin)))
				return result::omit;
			anchor at;
			if (part && s.mission.waist_picks && !vr::valid_hand(s.tools.holders[part - 1]))
				at = waist_root(attachments::body_frame(pose, origin), s, part - 1);
			else if (part && s.mission.waist_picks)
			{
				const auto h = unsigned(s.tools.holders[part - 1]);
				at = compose({add(pose.wrists[h].position, origin), pose.wrists[h].rotation},
				             pick_attachment(part - 1, h, s));
			}
			else if (!part && !vr::valid_hand(s.holder))
				at = detonator_stowed(attachments::body_frame(pose, origin), s.asset->center);
			else
			{
				const auto h = part ? part - 1 : unsigned(s.holder);
				at = compose({add(pose.wrists[h].position, origin), pose.wrists[h].rotation},
				             attachment(h, s, part != 0));
			}
			std::copy(at.position.begin(), at.position.end(), current.origin);
			std::copy(at.rotation.begin(), at.rotation.end(), current.quat);
			previous = current;
			return result::replace;
		}
		game::scr_string_t observe_notify(unsigned owner,
		                                  game::scr_string_t event,
		                                  const game::VariableValue*)
		{
			if (!enabled() || !map_supported())
				return 0;
			const auto* name = game::SL_ConvertToString(event);
			if (!name || std::string_view(name) != "detonate")
				return 0;
			try
			{
				if (owner != scripting::entity{game::scr_entref_t{0, 0}}.get_entity_id())
					return 0;
				const auto c = latest();
				if (c.authorized && c.native_c4)
				{
					detonated_at = game::CG_GetGameTime(0);
					++native_detonations;
				}
			}
			catch (...)
			{
				++failures;
			}
			return 0; // Observation only: original event/arguments remain authoritative.
		}
		void reset_level()
		{
			++generation;
			detonated_at = -1;
			mission = {};
			stow();
			stow_picks();
			tools.reset();
			pick_continuity = 0;
			oilrig_selected = false;
			epoch = reference = 0;
			last_time = 0;
			publish();
		}
	}
	context latest() noexcept
	{
		const std::lock_guard lock(mutex);
		return published.mission;
	}
	pick_geometry geometry() noexcept
	{
		const std::lock_guard lock(mutex);
		pick_geometry out;
		out.assets = bool(published.asset);
		out.hands_ready = published.hands_ready;
		out.basis = published.basis;
		out.attachment = {authored::pick_l_attachment, authored::pick_r_attachment};
		if (published.asset)
			out.tip = published.asset->pick_tips;
		return out;
	}
	bool active() noexcept
	{
		return enabled() && latest().active;
	}
	bool current_pickaxe(vr::hand h, unsigned side, std::uint64_t lease, std::uint64_t ref) noexcept
	{
		if (!enabled() || !cheats::waist_pickaxes() || cheats::transitioning() || side >= 2 ||
		    !vr::valid_hand(h))
			return false;
		const std::lock_guard lock(mutex);
		const auto& s = published;
		return s.mission.waist_picks && !s.mission.native_body && s.asset && fresh(s.at) &&
		       s.reference == ref && s.tools.holders[side] == h && s.tools.lease(side) == lease;
	}
	bool melee_pickaxe(vr::hand h, const anchor& wrist, std::uint64_t ref, pickaxe_strike& out) noexcept
	{
		if (!enabled() || !cheats::waist_pickaxes() || cheats::transitioning() || !hi::allows_melee(h))
			return false;
		snapshot s;
		{
			const std::lock_guard lock(mutex);
			s = published;
		}
		const auto side = s.tools.held(h);
		if (side < 0 || !s.mission.waist_picks || s.mission.native_body || s.mission.picks || !s.asset ||
		    !s.hands_ready || !fresh(s.at) || s.reference != ref || !pick_weapon ||
		    !(s.tracked & (1u << unsigned(h))))
			return false;
		if (::vr::gameplay::cliffhanger_physical::independent_hands() ||
		    hi::pose(h).driver != hi::object(hi::domain::special,
		                                     {pick_weapon.load(), s.tools.lease(unsigned(side))},
		                                     40 + unsigned(side)))
			return false;
		auto w = wrist;
		w.rotation = normalize(multiply(w.rotation, s.basis[unsigned(h)]));
		out = {compose(w, pick_attachment(unsigned(side), unsigned(h), s)),
		       pick_weapon.load(),
		       unsigned(side),
		       s.tools.lease(unsigned(side))};
		return true;
	}
	bool native_c4_session() noexcept
	{
		const auto c = latest();
		if (!enabled() || !c.active || !c.detonator || !c.weapon || !game::CL_IsCgameInitialized())
			return false;
		// Includes the native raising/drop interval. Retained VR guns must not
		// emit independent shots while the story has selected its detonator.
		return (field<unsigned>(game::CG_GetPredictedPlayerState(0), 0x3bc) & 511) == c.weapon ||
		       field<unsigned>(vr::h2::sp::weapon_selection_request.get(), 0) == c.weapon;
	}
	bool preserve_native_selection(unsigned requested) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || !native_c4_session())
			return false;
		const auto c = latest();
		if (!protects_selection(true, c.weapon, requested))
			return false;
		if (c.oilrig)
			return vr::valid_hand(holder) &&
			       sequences::oilrig::current_equipment() == sequences::oilrig::equipment::detonator;
		try
		{
			// Selection requests are infrequent. Recheck the live cue here so a
			// same-tick holster release cannot race the ordinary publication.
			if (!*game::levelEntityId)
				return false;
			const auto flags = scripting::entity{*game::levelEntityId}.get("flag");
			if (!flags.is<scripting::array>())
				return false;
			const auto cue = flags.as<scripting::array>().get(std::string("player_can_see_capture"));
			return cue.is<int>() && cue.as<int>() != 0;
		}
		catch (...)
		{
			++failures;
			return true;
		}
	}
	void observe()
	{
		context next{};
		if (!enabled() || !map_supported() || !*game::levelEntityId)
		{
			mission = next;
			publish();
			return;
		}
		const auto* ps = game::g_entities[0].client;
		if (!ps || player_life::dead(ps))
		{
			mission = next;
			publish();
			return;
		}
		const scripting::entity level{*game::levelEntityId}, player{game::scr_entref_t{0, 0}};
		const auto values = level.get("flag"), start = level.get("start_point");
		const bool oilrig = sequences::oilrig::supported();
		if (!values.is<scripting::array>() || (!oilrig && !start.is<std::string>()))
		{
			mission = next;
			publish();
			return;
		}
		const auto flags = values.as<scripting::array>();
		const auto flag = [&](const char* name)
		{
			const auto v = flags.get(std::string(name));
			return v.is<int>() && v.as<int>() != 0;
		};
		next.active = true;
		next.generation = generation.load();
		next.time = game::CG_GetGameTime(0);
		next.detonator = abdominal_enabled();
		next.oilrig = oilrig;
		next.opening = !oilrig && opening_start(start.as<std::string>()) && !flag("reached_top");
		const bool weapons_enabled = scripted_control::permits_weapons(field<unsigned>(ps, 0x3c0));
		// The native worldbody, its arms, picks and movement keep ownership.
		// Props can bind to tracked hands only after that ownership ends.
		next.native_body = !weapons_enabled;
		if (player.call("islinked").as<int>())
		{
			const auto parent = player.call("getlinkedparent");
			if (parent.is<scripting::entity>())
			{
				const auto anim = parent.as<scripting::entity>().get("animname");
				next.native_body = next.native_body || next.opening ||
				                   (anim.is<std::string>() && anim.as<std::string>() == "worldbody");
			}
		}
		const auto selected_name = weapon_name(field<unsigned>(ps, 0x3bc) & 511);
		const bool climbing = !oilrig && ::vr::gameplay::cliffhanger_physical::independent_hands();
		if (climbing)
		{
			next.native_body = false;
			next.detonator = false;
		}
		next.picks = climbing || (next.opening && !next.native_body && pick_definition(selected_name.data()));
		next.waist_picks =
		    !oilrig && waist_picks_allowed(
		                   cheats::waist_pickaxes(), next.native_body, next.picks, cheats::transitioning());
		if (!next.waist_picks)
			stow_picks();
		next.weapon = mission.generation == next.generation ? mission.weapon : 0;
		if (!next.weapon)
			for (unsigned i = 1; i < 512; ++i)
			{
				if (std::string_view(weapon_name(i).data()) == "c4")
				{
					next.weapon = i;
					break;
				}
			}
		next.native_c4 = next.weapon && (field<unsigned>(ps, 0x3bc) & 511) == next.weapon;
		next.consumed = oilrig ? flag("ambush_c4_triggered")
		                       : flag("player_detonate") || flag("tarmac_escape") ||
		                             flag("player_starts_snowmobile_trip");
		if (oilrig)
		{
			next.native_body = !scripted_control::allowed(ps);
			next.detonator = next.detonator && sequences::oilrig::current_equipment() ==
			                                       sequences::oilrig::equipment::detonator;
			for (unsigned n = 0; n < 15; ++n)
				next.owns_c4 =
				    next.owns_c4 || (next.weapon && field<unsigned>(ps, 0x2f8 + n * 4) == next.weapon);
			next.detonator = next.detonator && next.weapon;
			// Shared C4 script 43691::_id_CC10 gives the native item on planting
			// and sets its CLIP to zero. It does not require zero reserve ammo.
			// ITEM weapons have no physical clip-ledger entry; use the native
			// script getter rather than treating a missing primary ledger as empty.
			const auto clip = next.owns_c4 ? player.call("getweaponammoclip", {std::string{"c4"}})
			                               : scripting::script_value{};
			next.authorized =
			    next.detonator && !next.native_body &&
			    sequences::oilrig::can_detonate({.planted = flag("obj_c4_ambush_plant_complete"),
			                                     .triggered = next.consumed,
			                                     .native_c4 = next.native_c4,
			                                     .weapons_enabled = weapons_enabled,
			                                     .empty_feed = clip.is<int>() && clip.as<int>() == 0});
		}
		else
			next.authorized =
			    next.detonator && !next.native_body &&
			    can_detonate(flag("player_can_see_capture"), next.native_c4, weapons_enabled, next.consumed);
		next.detonated_at = detonated_at.load();
		mission = next;
		if (climbing)
		{
			hi::frame f;
			f.input = controller_input::latest();
			if (head_pose_bridge::get_spatial_frame(f.body) &&
			    f.body.generation == f.input.reference_generation)
			{
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
					if (tracked_wrist(f.input, f.body, {}, h, offsets, f.wrists[h]))
						f.valid_hands |= 1u << h;
				publish(&f);
				return;
			}
		}
		publish();
	}
	void initialize()
	{
		scheduler::loop(
		    []
		    {
			    try
			    {
				    observe();
			    }
			    catch (...)
			    {
				    mission = {};
				    publish();
				    ++failures;
			    }
		    },
		    scheduler::pipeline::server);
		scheduler::loop(refresh, scheduler::pipeline::main, 250ms);
		scene_models::on_submit(submit);
		scene_models::on_prepare_placement(prepare);
		scripting::on_level_start(reset_level);
		scripting::on_shutdown(
		    [](bool, bool after)
		    {
			    if (!after)
				    reset_level();
		    });
		scripting::on_notify_alias(observe_notify);
		::command::add(
		    "vr_cliffhanger_status",
		    []
		    {
			    scheduler::once(
			        []
			        {
				        const auto value = status();
				        console::print_text(console::con_type_info, value);
				        scheduler::once(
				            [value]
				            { utils::io::write_file_atomic("minidumps/overlord-cliffhanger.txt", value); },
				            scheduler::pipeline::async);
			        },
			        scheduler::pipeline::server);
		    });
	}
	void retire()
	{
		assets_ready = false;
		stow();
		stow_picks();
		tools.reset();
		pick_weapon = 0;
		pick_continuity = 0;
		oilrig_selected = false;
		mission = {};
		attempted = nullptr;
		asset.reset();
		{
			const std::lock_guard lock(mutex);
			published = {};
			submitted = {};
		}
		++generation;
		detonated_at = -1;
	}
	bool collect(const hi::frame& f, bool provider_enabled) noexcept
	{
		if (::vr::gameplay::cliffhanger_physical::independent_hands())
		{
			stow_picks();
			publish();
			return true;
		}
		mission = latest();
		const bool body_tools = enabled() && weapons::carry::active() && cheats::waist_pickaxes();
		if ((!provider_enabled && !body_tools) || !mission.active || mission.native_body ||
		    (mission.oilrig && (!mission.detonator || mission.consumed)))
		{
			stow();
			stow_picks();
			publish(&f);
			restore_oilrig_selection();
			return false;
		}
		if (epoch != mission.generation || reference != f.input.reference_generation ||
		    mission.time < last_time || pick_continuity != f.input.continuity_generation)
		{
			stow();
			stow_picks();
			epoch = mission.generation;
			pick_continuity = f.input.continuity_generation;
		}
		reference = f.input.reference_generation;
		last_time = mission.time;
		if (mission.picks || !mission.detonator ||
		    (mission.consumed && (mission.detonated_at < 0 || mission.time - mission.detonated_at > 850)))
			stow();
		publish(&f);
		snapshot s;
		{
			const std::lock_guard lock(mutex);
			s = published;
		}
		collect_picks(f, s);
		if (!provider_enabled || !s.asset || !s.hands_ready || mission.picks || mission.consumed ||
		    !mission.detonator)
			return true;
		const auto pending = abdominal_intent(intention, f);
		for (unsigned h = 0; h < 2; ++h)
		{
			const auto actor = vr::hand(h);
			const auto edge = hi::input(actor, hi::button::grip);
			if (vr::valid_hand(holder) || !edge.down || !(pending & (1u << h)) || !hi::free(actor) ||
			    !(f.valid_hands & (1u << h)))
				continue;
			const float d = abdominal_grab_distance(f.body, f.wrists[h], s.basis[h], s.mirror[h], h);
			if (d <= 1)
				hi::offer({actor,
				           {hi::object(hi::domain::special, {mission.weapon, revision + 1}, 30),
				            hi::role::control,
				            hi::button::grip,
				            hi::recipe::single,
				            hi::capability::action},
				           edge.event,
				           20,
				           d,
				           1,
				           true,
				           true});
		}
		return true;
	}
	void update() noexcept
	{
		const auto* f = hi::simulation();
		if (!f || !mission.active)
			return;
		update_picks(*f);
		for (unsigned h = 0; h < 2; ++h)
			if (!mission.picks && !mission.consumed && !vr::valid_hand(holder) &&
			    hi::granted(vr::hand(h),
			                hi::domain::special,
			                {mission.weapon, revision + 1},
			                hi::button::grip,
			                hi::role::control))
			{
				if (mission.oilrig && mission.owns_c4 && !weapons::native_carry::select(mission.weapon))
					continue;
				if (mission.oilrig && mission.owns_c4)
					oilrig_selected = true;
				holder = vr::hand(h);
				++revision;
				++takes;
				weapons::feedback::carry_confirmation(holder, f->input);
			}
		if (vr::valid_hand(holder))
		{
			const auto h = unsigned(holder);
			const auto edge = hi::input(holder, hi::button::grip);
			if (!(f->valid_hands & (1u << h)) || edge.release ||
			    (f->input.squeeze[h].active && !f->input.squeeze[h].down) ||
			    weapons::carry::hand_has_weapon(holder))
				stow();
		}
		if (mission.oilrig && mission.owns_c4 && vr::valid_hand(holder) &&
		    weapons::native_carry::select(mission.weapon))
			oilrig_selected = true;
		publish(f);
		restore_oilrig_selection();
	}
	void report() noexcept
	{
		if (!mission.active || mission.native_body || !assets_ready)
			return;
		if (mission.picks)
			for (unsigned h = 0; h < 2; ++h)
				hi::observed(vr::hand(h),
				             {target(h + 1),
				              hi::role::control,
				              hi::button::none,
				              hi::recipe::single,
				              hi::capability::action});
		if (mission.waist_picks && cheats::waist_pickaxes() && !cheats::transitioning())
			for (unsigned side = 0; side < 2; ++side)
				if (vr::valid_hand(tools.holders[side]))
					hi::observed(tools.holders[side], pick_grasp(side));
		if (vr::valid_hand(holder))
			hi::observed(
			    holder,
			    {target(), hi::role::control, hi::button::grip, hi::recipe::single, hi::capability::action});
	}
	void lifecycle(bool suspended) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server))
			return;
		const auto c = latest();
		const auto input = controller_input::latest();
		if (!c.active || !enabled() || !input.focused || !fresh(input.sampled_at) ||
		    (reference && reference != input.reference_generation) || suspended)
		{
			stow();
			stow_picks();
			publish();
		}
		else if (!cheats::waist_pickaxes() || cheats::transitioning() || !c.waist_picks)
		{
			stow_picks();
			publish();
		}
	}
	void command(const controller_input::frame& input, bool gameplay, int& buttons) noexcept
	{
		snapshot s;
		{
			const std::lock_guard lock(mutex);
			s = published;
		}
		if (!enabled() || !s.mission.active)
		{
			trigger.reset();
			return;
		}
		// Only the script-selected zero-ammo C4 owns attack. Before the cue,
		// holding the cosmetic prop never changes another gun's command.
		if (native_c4_session())
			buttons &= ~1;
		const bool held = vr::valid_hand(s.holder);
		const auto* ps = game::CL_IsCgameInitialized() ? game::CG_GetPredictedPlayerState(0) : nullptr;
		// The two physical tools own melee; native Trigger must not run the
		// flat viewmodel's forward melee search while this definition is selected.
		if (cheats::waist_pickaxes() && pick_weapon && ps &&
		    (field<unsigned>(ps, 0x3bc) & 511) == pick_weapon)
			buttons &= ~1;
		const bool usable =
		    gameplay && held && s.asset && fresh(s.at) && input.focused && !input.orientation_settling &&
		    fresh(input.sampled_at) && s.reference == input.reference_generation &&
		    input.grip[unsigned(s.holder)].valid && input.aim[unsigned(s.holder)].valid && ps &&
		    (field<unsigned>(ps, 0x3bc) & 511) == s.mission.weapon && scripted_control::predicted_allowed() &&
		    field<unsigned>(ps, 0x2c0) == 0 && field<int>(ps, 0x2b4) <= 0;
		if (trigger.consume(input, s.mission, s.holder, s.revision, usable))
			buttons |= 1;
	}
	void present(const hands::interaction_rig& parts,
	             const rig& r,
	             const controller_input::frame& input,
	             const std::array<anchor, 2>& targets,
	             const std::array<vec, 2>&,
	             const std::array<vec, 3>&,
	             float,
	             std::span<bone> solved,
	             unsigned occupied,
	             unsigned visible_hands) noexcept
	{
		if (!parts.valid || r.count <= 0 || r.count > 256 || solved.size() < std::size_t(r.count))
			return;
		const bool climbing = ::vr::gameplay::cliffhanger_physical::independent_hands();
		snapshot s;
		{
			const std::lock_guard lock(mutex);
			published.basis = parts.basis;
			published.hands_ready = true;
			if (climbing)
			{
				published.at = input.sampled_at;
				published.reference = input.reference_generation;
				published.tracked = (input.grip[0].valid ? 1u : 0u) | (input.grip[1].valid ? 2u : 0u);
			}
			for (unsigned h = 0; h < 2; ++h)
				published.mirror[h] = parts.library.mirror_basis[r.arms[h].wrist];
			s = published;
		}
		if (!s.mission.active || s.mission.native_body || !s.asset || !fresh(s.at) ||
		    s.reference != input.reference_generation)
			return;
		for (unsigned h = 0; h < 2; ++h)
		{
			const auto tool = s.mission.waist_picks ? s.tools.held(vr::hand(h)) : -1;
			if (!(visible_hands & (1u << h)) || (occupied & (1u << h)) ||
			    (!s.mission.picks && s.holder != vr::hand(h) && tool < 0))
				continue;
			// A world support is authoritative even beyond the ordinary arm
			// reach limit. Preserve the solved shoulder/elbow, but place the
			// hand and rigid pick at the constrained wrist, not the IK cap.
			// This also retains the wall clearance of an unlatched pick.
			move_part(r,
			          r.arms[h].wrist,
			          {climbing ? targets[h].position : solved[r.arms[h].wrist].position,
			           normalize(multiply(targets[h].rotation, parts.basis[h]))},
			          solved);
			if (s.mission.picks || tool >= 0)
			{
				const auto side = tool >= 0 ? unsigned(tool) : h;
				hands::pose_mirror::fingers(r,
				                            parts.library,
				                            hands::native_hand_schema::definition,
				                            side ? std::span{authored::pick_r_fingers}
				                                 : std::span{authored::pick_l_fingers},
				                            h,
				                            solved,
				                            side != h);
			}
			else
				hands::pose_mirror::fingers(r,
				                            parts.library,
				                            hands::native_hand_schema::definition,
				                            authored::detonator_fingers,
				                            h,
				                            solved,
				                            h == 0);
		}
	}
	std::string status()
	{
		const std::lock_guard lock(mutex);
		const auto& s = published;
		return std::format(
		           "[VR mission props] active={} opening={} picks={} detonator={} authorized={} native_c4={} consumed={} holder={} epoch={} resources={} takes={} returns={} native_detonations={} failures={} reason={} oilrig={} owns_c4={}\n",
		           s.mission.active,
		           s.mission.opening,
		           s.mission.picks,
		           s.mission.detonator,
		           s.mission.authorized,
		           s.mission.native_c4,
		           s.mission.consumed,
		           int(s.holder),
		           s.mission.generation,
		           assets_ready.load(),
		           takes.load(),
		           returns.load(),
		           native_detonations.load(),
		           failures.load(),
		           reason.load(),
		           s.mission.oilrig,
		           s.mission.owns_c4) +
		       std::format(
		           "green_beret_picks={} weapon={} waist_ready={} holders={}/{} leases={}/{} takes={} returns={}\n",
		           s.mission.waist_picks,
		           pick_weapon.load(),
		           s.waist_ready,
		           int(s.tools.holders[0]),
		           int(s.tools.holders[1]),
		           s.tools.lease(0),
		           s.tools.lease(1),
		           pick_takes.load(),
		           pick_returns.load());
	}
}
