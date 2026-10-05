#include <std_include.hpp>
#include "../debug_options.hpp"
#include "component/scene_models.hpp"
#include "native_scripted_control.hpp"
#include "physical_reload_runtime.hpp"
#include "quick_reload_runtime.hpp"
#include "physical_reload_presenter.hpp"
#include "native_shot_history.hpp"
#include "weapon_carry_runtime.hpp"
#include "carry_interaction.hpp"
#include "weapon_runtime_lifecycle.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_interaction.hpp"
#include "native_ammo_grant.hpp"
#include "weapon_feedback.hpp"
#include "weapon_reload_profiles.hpp"
#include "hand_interaction/runtime.hpp"
#include "physical_reload_contact_sample.hpp"
#include "reload_item_runtime.hpp"
#include "reload_item_compatibility.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <mutex>
#include <sstream>

namespace vr::gameplay::weapons::physical_reload
{
	namespace
	{
		std::atomic<unsigned> boundaries{};
		std::atomic_bool alive{true};
		game::dvar_t* enabled{};
		std::mutex mutex;
		scene_frame last_published_scene{}; // Diagnostics never drive mechanics.
		instance_cache<scene_frame,carry::inventory::capacity> scenes;
		struct instance
		{
			struct attempt
			{
				clock::time_point at{};
				geometry contact{};
				const char* decision{};
				int held{}; bool trigger{}, squeeze{}, slide{}; float travel{};
			};
			presentation view{};
			std::array<char,64> native_name{}; // exact instance identity even when shared family data is used
			controller gesture{};
			std::uint64_t assembly{};
			scene_frame last_scene{};
			std::array<attempt,32> attempts{};
			std::uint64_t attempt_count{}, last_input{};
			std::array<std::uint64_t,static_cast<size_t>(mechanics::effect::count)> effects{};
		};
		std::array<instance, 15> inventory{};
		native_shot_history history{};
		std::uint64_t generation{1}, commits{}, failures{}, shots{};
		std::uint64_t server_ticks{};
		std::uint64_t native_grants{};
		native_ammunition::reload_snapshot latest_native{};
		const void* server_ps{};
		std::uint64_t server_timeline{};
		int game_time{};
		bool script_suspended{};
		const char* reason{"waiting for native and presentation contracts"};
		bool active() noexcept { return alive.load() && boundaries.load() == 7 && enabled && enabled->current.enabled; }
		instance* find(weapon_identity id) noexcept
		{
			for (auto& value : inventory) if (value.view.active && value.view.owner.id() == id) return &value;
			return nullptr;
		}
		void clear() noexcept
		{
			inventory = {}; scenes = {}; last_published_scene = {}; history = {}; ++generation;
		}
		void emit_feedback(const instance& value, mechanics::effect kind)
		{
			const auto& s = value.last_scene;
			const auto now = clock::now();
			if (!s.gameplay || !s.contact.valid || s.owner.id() != value.view.owner.id() ||
				now < s.input.sampled_at || now-s.input.sampled_at > 100ms) return;
			const auto position = kind == mechanics::effect::magazine_draw ? s.held_world.position : s.attached_world.position;
			feedback::event event{kind,s.owner,s.input.reference_generation,now,position,value.view.definition,value.view.ammo.instance_generation};
			if (kind==mechanics::effect::case_eject) {event.brass=s.ejection_world;event.has_brass=true;event.position=event.brass.position;}
			feedback::publish(event);
		}
		bool commit(instance& value, const mechanics::transaction& tx, bool direct_load=false)
		{
			reload_items::key released;
			if(tx.item_released)
			{
				const auto& s=value.last_scene;
				released=reload_items::reserve(value.view.owner.id(),value.view.definition,tx.rounds_to_item,
					tx.feedback==mechanics::effect::magazine_out?s.attached_world:s.held_world,s.units_per_meter,
					s.input.reference_generation,clock::now(),tx.feedback==mechanics::effect::magazine_out);
				if(!released)return false;
			}
			if (!native_ammunition::compare_commit_owned(server_ps,value.view.owner.id(),tx.before,tx.after))
			{
				if(released)reload_items::complete_release(released,false);
				++failures; reason = "native compare rejected; transaction not published";
				return false;
			}
			++commits;
			if(released)reload_items::complete_release(released,true);
			// Continuous manual-bolt poses commit mechanically but are not feedback events.
			if (tx.feedback==mechanics::effect::none || tx.silent) return true;
			++value.effects[static_cast<size_t>(tx.feedback)];
			value.view.effect = tx.feedback;
			value.view.effect_at = clock::now();
			++value.view.effect_sequence;
			value.view.events[value.view.effect_sequence % value.view.events.size()] =
				{value.view.effect_sequence, tx.feedback, value.view.effect_at,
				 tx.feedback == mechanics::effect::live_eject ? value.last_scene.ejection_world :
				 direct_load || tx.feedback == mechanics::effect::magazine_out ? value.last_scene.attached_world : value.last_scene.held_world,
				 value.last_scene.units_per_meter,value.last_scene.attached_world,
				 tx.feedback==mechanics::effect::magazine_out ? value.view.ammo.magazine_rounds : value.view.ammo.held_rounds,tx.item_released};
			if (!tx.silent) emit_feedback(value, tx.feedback);
			return true;
		}
		bool interrupt(instance& value)
		{
			value.view.quick_load.reset();
			const bool ok = value.gesture.interrupt(value.view.definition->ammunition, value.view.ammo, value.view.owner.holding_hand(),
				[&](const auto& tx) { return commit(value, tx); });
			value.view.slide_held = false;
			value.view.belt_grip={};
			value.view.magazine_seated = false;
			value.view.magazine_grabbed = false;
			value.view.examined = {};
			value.view.slap_diagnostics = {};
			value.view.well_contact = value.view.requires_withdrawal = false;
			value.view.slide_travel = minimum_slide_travel(value.view.definition->interaction,value.view.definition->ammunition,value.view.ammo);
			value.view.handle_amount=value.view.ammo.action==mechanics::action_state::latched_open ? 1.f : 0.f;
			value.view.handle_grip={};
			return ok;
		}
		bool reconcile(instance& value, const native_ammunition::reload_snapshot& observed)
		{
			if (value.view.fault) return false;
			if (value.native_name!=observed.native_name ||
				!value.view.definition->matches_native(observed.native_name.data(),observed.base_capacity))
			{ value.view.fault = true; ++failures; reason = "owned weapon profile changed; instance blocked"; return false; }
			if (observed.ammo.loaded != mechanics::native_ammo(value.view.ammo).loaded)
			{
				const auto grant = mechanics::reconcile_native_grant(value.view.definition->ammunition,value.view.ammo,
					{observed.ammo.loaded,observed.ammo.reserve},1000000);
				if (grant.valid)
				{
					if (!native_ammunition::commit_owned(observed.ammo,grant.after.loaded,grant.after.reserve))
					{ ++failures; reason = "native grant compare rejected; state unchanged"; return false; }
					value.view.ammo = grant.next;
					++native_grants;
					reason = "native ammo grant reconciled to reserve; physical feed preserved";
					return true;
				}
				value.view.fault = true;
				reason = "external loaded-count mutation; physical instance blocked until reset/level change";
				++failures;
				return false;
			}
			// Native pickups and other weapons may share the reserve pool. Only the
			// pool reconciles automatically; loaded totals cannot infer the chamber.
			if (value.view.ammo.reserve_rounds != observed.ammo.reserve)
			{
				value.view.ammo.reserve_rounds = observed.ammo.reserve;
				++value.view.ammo.revision;
			}
			return true;
		}
		void update_instance(const void* ps, scene_frame scene, const native_ammunition::reload_snapshot& observed, clock::time_point now)
		{
			const auto selected = observed.ammo.weapon;
			auto* value = find(scene.owner.id());
			const bool bootstrap=!value;
			if (value && !reconcile(*value,observed)) return; // Refresh shared reserve after the other instance's transaction.
			// Admit a mapped instance from its own complete scene. Native selection
			// and its equip timer have no authority over a carried instance.
			// Existing instances remain mechanical during tracking loss; never let
			// native auto-reload silently refill a physically absent magazine.
			if (!value)
			{
				const auto* definition = native_reload_profile(observed.native_name.data(),observed.base_capacity,scene.definition);
				int live_capacity{};
				const auto blocked = [&]() -> const char* {
					if (!definition)
						return "current native weapon has no physical profile";
					if (scene.definition != definition) return "native and scene weapon profiles do not match";
					if (definition->interaction.manual_bolt && !observed.bolt_action) return "manual bolt requires native bolt-action descriptor";
					if (!carry::active() && observed.ammo.weapon_state != 0) return "waiting for native weapon idle";
					if (!scene.contact.valid || !scene.assembly) return "waiting for complete mechanical viewmodel geometry";
					if (scene.owner.id() != observed.ammo.id() || !valid_hand(scene.owner.holding_hand()))
						return "scene weapon/rear-hand does not match authored profile";
					if (!scene.gameplay) return "scene input is paused or captured by UI";
					if (now < scene.input.sampled_at || now-scene.input.sampled_at > 150ms)
						return "waiting for fresh tracked scene input";
					live_capacity=utils::hook::invoke<int>(0x1406A3A60,ps,selected,false);
					if (live_capacity != definition->ammunition.magazine_capacity)
						return "modified native capacity rejected";
					return nullptr;
				}();
				if (blocked) { reason = blocked; return; }
				for (auto& candidate : inventory) if (!candidate.view.active) { value = &candidate; break; }
				const auto initial=import_native_feed(*definition,scene.owner.id(),observed,live_capacity,generation+1);
				if (value && initial)
				{
					*value = {};
					value->view.active = true;
					value->view.definition = definition;
					value->native_name = observed.native_name;
					++generation;
					value->view.ammo = *initial;
					value->view.owner = scene.owner;
					value->assembly = scene.assembly;
					reason = "authored physical weapon instance admitted";
				}
				else { value = nullptr; reason = "inventory full or native feed initialization rejected"; }
			}
			if (!value || value->view.fault) return;
			if (bootstrap)
			{
				// The first accepted input owns a real domain generation now. Rebuild
				// its geometry from this batch's immutable tracking instead of losing
				// the granted edge while waiting for a renderer acknowledgement.
				const auto* input=hand_interaction::simulation();
				if (!input || !sample_contact(scene,value->view,*input)) return;
			}
			if (value->assembly != scene.assembly)
			{
				if (!interrupt(*value)) return;
				value->assembly = scene.assembly;
			}
			value->view.owner = scene.owner.id() == observed.ammo.id() ? scene.owner : value->view.owner;
			auto contact = scene.contact;
			if (scene.definition != value->view.definition) contact.valid = false;
			if (contact.valid && contact.weapon == selected && contact.instance_generation == value->view.ammo.instance_generation)
				value->last_scene = scene;
			// Every gesture uses the admitted generation and this batch's input.
			const bool diagnose=debug_options::enabled(debug_options::probe::weapon_events);
			const bool was_busy = diagnose && value->gesture.offhand_busy(value->view.ammo);
			auto rules=value->view.definition->ammunition;rules.discard_penalty=reload_items::discard_penalty();
			value->gesture.update(value->view.definition->interaction, rules, value->view.ammo, scene.input,
				value->view.owner, contact, scene.gameplay && scene.owner.id() == observed.ammo.id(), now,
				[&](const auto& tx) { return commit(*value, tx); }, {scene.manipulation,
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::magazine,scene.owner.id()),
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::magazine,scene.owner.id(),hand_interaction::button::trigger,hand_interaction::role::supply),
				hand_interaction::input(hand(1-int(scene.owner.holding_hand())),hand_interaction::button::trigger).press,
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::magazine,scene.owner.id(),hand_interaction::button::trigger,hand_interaction::role::part),hand_interaction::input(hand(1-int(scene.owner.holding_hand())),hand_interaction::button::trigger).release,
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::magazine,scene.owner.id(),hand_interaction::button::trigger,hand_interaction::role::supply),
				hand_interaction::input(hand(1-int(scene.owner.holding_hand())),hand_interaction::button::grip).release,
				hand_interaction::input(scene.owner.holding_hand(),hand_interaction::button::secondary).press,
				reload_items::can_release(value->view.definition)});
			value->view.slide_held = value->gesture.slide_held();
			if (value->gesture.feedback_effect() != mechanics::effect::none)
				emit_feedback(*value, value->gesture.feedback_effect());
			value->view.magazine_seated = value->gesture.magazine_seated();
			value->view.magazine_grabbed = value->gesture.magazine_grabbed();
			value->view.knife_magazine_grasp = value->gesture.knife_magazine_grasp();
			value->view.magazine_pose = value->gesture.magazine_pose();
			value->view.knife_slide_grasp = value->gesture.knife_slide_grasp();
			value->view.magazine_grab_start = value->gesture.magazine_grab_start();
			value->view.slide_travel = value->gesture.slide_travel();
			value->view.slide_grip = value->gesture.slide_grip();
			value->view.handle_amount=value->gesture.handle_amount();
			value->view.belt_grip=value->gesture.belt_grip();
			value->view.belt_push_reason=value->gesture.belt_push_reason();
			value->view.handle_grip=value->gesture.handle_grip();
			value->view.reference_generation = scene.input.reference_generation;
			value->view.input_sequence = scene.input.sequence;
			value->view.examined = value->gesture.examined();
			value->view.slap_diagnostics=value->gesture.slap_diagnostics();
			value->view.decision = value->gesture.decision();
			value->view.well_contact = value->gesture.well_contact();
			value->view.requires_withdrawal = value->gesture.requires_withdrawal();
			value->view.insert_after=value->gesture.insertion_after();
			const auto& ammo=value->view.ammo;
			const auto quick=mechanics::plan(value->view.definition->ammunition,ammo,
				{mechanics::operation::quick_load,ammo.weapon,ammo.instance_generation,ammo.revision,
				 value->view.owner.holding_hand(),value->view.owner.holding_hand()});
			if(quick_reload::ready(value->view.quick_load,value->view.owner,ammo.instance_generation,scene.assembly,value->view.definition->id,
				scene.gameplay && scene.definition==value->view.definition && bool(quick),now) && commit(*value,quick,true))
			{
				value->view.ammo=quick.next;value->view.quick_load.reset();
			}
			const int off = 1-static_cast<int>(value->view.owner.holding_hand());
			if (diagnose &&
				valid_hand(value->view.owner.holding_hand()) && scene.input.sequence != value->last_input &&
				contact.valid && scene.input.focused && scene.gameplay && now >= scene.input.sampled_at &&
				now-scene.input.sampled_at <= 150ms && (was_busy || scene.input.trigger[off].down))
			{
				const auto& last = value->attempts[value->attempt_count % value->attempts.size()];
				if (!value->attempt_count || last.decision != value->gesture.decision() || now-last.at >= 100ms)
				{
					++value->attempt_count;
					value->attempts[value->attempt_count % value->attempts.size()] = {now,contact,value->gesture.decision(),
						value->view.ammo.held_rounds,scene.input.trigger[off].down,scene.input.squeeze[off].down,
						value->view.slide_held,value->view.slide_travel};
				}
			}
			if(diagnose)value->last_input = scene.input.sequence;
		}
		void tick(bool interactions,bool suspended=false)
		{
			if (!alive.load() || !scheduler::is_executing(scheduler::pipeline::server)) return;
			const bool gameplay = game::CL_IsCgameInitialized();
			const auto* ps = gameplay ? game::g_entities[0].client : nullptr;
			const int time = gameplay ? game::CG_GetGameTime(0) : 0;
			const auto observed = ps ? native_ammunition::observe_reload(ps) : native_ammunition::reload_snapshot{};
			const auto now = clock::now();
			std::array<hold,2> owners{};
			if (carry::active()) { const auto held=carry::held_instances(); for (size_t h=0;h<held.size();++h) owners[h]=held[h].owner; }
			else owners[0]=current_hold();
			const std::lock_guard lock(mutex);
			++server_ticks;
			latest_native = observed;
			const auto timeline=native_ammunition::timeline();
			if (!ps || ps != server_ps || time < game_time || timeline!=server_timeline)
			{
				clear(); // checkpoint lifecycle isolation, NOT mid-reload persistence
				server_ps = ps;
				server_timeline=timeline;
				script_suspended=false;
			}
			game_time = time;
			if (!ps) { reason = "waiting for initialized local server player"; return; }
			if (!scripted_control::allowed(ps))
			{
				// Retain committed feed/escrow; forget gestures and pose leases so
				// a trigger or motion performed during the scene cannot replay.
				if (!script_suspended)
				{
					for (auto& value : inventory)
					{
						value.gesture={};value.view.quick_load.reset();value.view.slide_held=false;value.view.magazine_seated=false;value.view.magazine_grabbed=false;
						value.view.belt_grip={};value.view.handle_grip={};value.view.examined={};value.view.slap_diagnostics={};
					}
					scenes={};last_published_scene={};script_suspended=true;
				}
				reason="native script owns weapons";return;
			}
			script_suspended=false;
			for (auto& value : inventory)
			{
				if (!value.view.active) continue;
				const auto projected=carry::native_identity(value.view.owner.weapon);
				if (projection_rebind(value.view.owner.id(),projected) &&
					(projected.generation || native_ammunition::projected_identity(projected.weapon)==value.view.owner.id()))
				{
					const auto previous=value.view.owner.instance_generation;
					value.view.owner.instance_generation=projected.generation;
					// Return escrow through the new unique projection; retry the rebind
					// after a rejected write instead of exposing unfinished gestures.
					if (!interrupt(value)) {value.view.owner.instance_generation=previous;continue;}
				}
				const auto owned = native_ammunition::observe_owned(ps, value.view.owner.id());
				if (!owned.valid) { value = {}; continue; } // no writes to a removed weapon's slots
				if (!active() || !carry::active())
				{
					// Explicit feature exit normalizes to native semantics. Preserve
					// actual loaded counts even after an external-mutation fault; only
					// return our still-owned magazine escrow, never an extracted round.
					const auto escrow = value.view.ammo.held_rounds;
					if (escrow && !native_ammunition::commit_owned(owned.ammo,owned.ammo.loaded,owned.ammo.reserve+escrow))
					{ reason = "feature exit refund rejected; escrow retained"; ++failures; continue; }
					value = {};
					continue;
				}
				if (!reconcile(value, owned)) continue;
				const auto live = std::find_if(owners.begin(),owners.end(),[&](const hold& h){return h.id()==value.view.owner.id();});
				if (suspended || live == owners.end() || live->holding_hand() != value.view.owner.holding_hand() || live->rear_revision != value.view.owner.rear_revision)
				{
					if (!interrupt(value)) continue; // Retry this ledger without starving other instances.
				}
			}
			if (!interactions) return;
			if (!active()) { reason = "physical reload disabled or contracts incomplete"; return; }

			reason="waiting for held weapon contact scene";
			for (const auto& owner : owners)
			{
				const auto* cached=scenes.find(owner.id());
				if (!cached) continue;
				auto frame = *cached;
				if(const auto* input=hand_interaction::simulation())
				{
					const auto* value=find(owner.id());const auto view=value?value->view:presentation{};
					const auto actor=hand(1-int(owner.holding_hand()));
					frame.contact.knife_held=hand_interaction::has(actor,hand_interaction::domain::knife);
					if(!sample_contact(frame,view,*input)){frame.input=input->input;frame.owner=owner;frame.contact.valid=false;frame.gameplay=true;}
					const auto pose=frame.contact.knife_held && frame.definition->knife_magazine_in_wrist?hand_interaction::recipe::knife_magazine:hand_interaction::recipe::single;
					frame.manipulation=hand_interaction::permits(actor,hand_interaction::domain::magazine,owner.id(),pose,hand_interaction::role::supply);
				}
				if (frame.owner.rear_revision != owner.rear_revision || frame.owner.holding_hand() != owner.holding_hand()) continue;
				const auto owned = native_ammunition::observe_owned(ps,owner.id());
				if (owned.valid) {reason="processing held weapon contact scene";update_instance(ps,frame,owned,now);}
			}
		}
	}
	void collect_interactions(const hand_interaction::frame& input)noexcept
	{
		namespace hi=hand_interaction;
		const std::lock_guard lock(mutex);
		for(const auto& entry:scenes.entries())if(entry.id)
		{
			auto s=entry.value;const auto* live=input.find(entry.id);if(!live || !valid_hand(live->owner.holding_hand()))continue;
			const auto actor=hand(1-int(live->owner.holding_hand()));const auto edge=hi::input(actor,hi::button::trigger);
			const auto release=hi::input(live->owner.holding_hand(),hi::button::secondary);
			const auto grip=hi::input(actor,hi::button::grip);
			const bool support_catch=s.definition && s.definition->interaction.support_magazine_catch &&
				live->owner.can_fire() && live->owner.support==actor && release.press && grip.down && !grip.release && edge.down && !edge.release;
			if(!support_catch && (!edge.press || !edge.down))continue;
			const auto* value=find(entry.id);auto view=value?value->view:presentation{};
			if (!view.active)
			{
				// First-frame contacts need the native feed's real shape (including
				// an inserted manual magazine), not zero-initialized mechanical state.
				const auto* ps=game::g_entities[0].client;
				const auto observed=native_ammunition::observe_owned(ps,entry.id);
				const auto* definition=observed.valid?native_reload_profile(observed.native_name.data(),observed.base_capacity,s.definition):nullptr;
				if (!definition) continue;
				const auto preview=import_native_feed(*definition,entry.id,observed,
					utils::hook::invoke<int>(0x1406A3A60,ps,entry.id.weapon,false),generation+1);
				if (!preview) continue;
				view.active=true;view.definition=definition;view.owner=live->owner;view.ammo=*preview;
			}
			s.contact.knife_held=hi::has(actor,hi::domain::knife);if(!sample_contact(s,view,input))continue;
			const auto& g=s.contact;const auto& p=s.definition->interaction;
			if(support_catch)
			{
				if(view.fault || view.part_leased() || !view.ammo.magazine_inserted || g.knife_held ||
					g.seated_hand_distance>p.part_release_distance || g.attached_magazine_pose>=p.magazine_pose_count)continue;
				hi::offer({actor,{hi::object(hi::domain::magazine,entry.id,0,s.assembly),hi::role::supply,hi::button::trigger,hi::recipe::single,{}},
					release.event,10,0,1,true,true,hi::object(hi::domain::carry,entry.id,1)});
				continue;
			}
			float part=g.slide_distance/p.slide_radius;
			if(attached_magazine_radius(p)>0 && valid(g.magazine) && view.ammo.magazine_inserted)
				part=std::min(part,g.magazine.grip_distance/attached_magazine_radius(p));
			if(p.belt && belt_feed::preferred(*p.belt,view.ammo.belt,view.ammo.magazine_inserted,view.ammo.magazine_rounds,g.belt,g.slide_distance,g.magazine.grip_distance))part=0;
			const bool supply=part>1 && g.waist_distance<=p.waist_radius;
			const float distance=supply?g.waist_distance/p.waist_radius:part;
			if(!std::isfinite(distance) || distance>1)continue;
			const auto pose=s.contact.knife_held?(supply && s.definition->knife_magazine_in_wrist?hi::recipe::knife_magazine:!supply && !s.definition->knife_slide_grips.empty()?hi::recipe::knife_part:hi::recipe::single):hi::recipe::single;
			hi::offer({actor,{hi::object(hi::domain::magazine,entry.id,0,s.assembly),supply?hi::role::supply:hi::role::part,hi::button::trigger,pose,{}},edge.event,supply?30u:20u,distance,1,true,true});
		}
	}
	void update_interactions(){tick(true);}
	void reconcile_carry_support() noexcept
	{
		bool changed{};
		for (const auto& item : carry::interaction_instances())
		{
			if (item.at != carry::location::held || !item.owner.can_fire() || !valid_hand(item.owner.support))
				continue;
			if (takes_carry_support(current(item.id), item.owner))
				changed = carry::release_support(item.id) || changed;
		}
		if (changed)
			carry::publish_topology();
	}
	bool exchange_supply(
	    weapon_identity id, hand actor, bool draw, supply_commit_fn write, void* context) noexcept
	{
		namespace hi=hand_interaction;const auto* input=hi::simulation();const auto now=clock::now();
		if(!active() || !input || !write || !valid_hand(actor) || !input->input.focused ||
			now<input->input.sampled_at || now-input->input.sampled_at>150ms)return false;
		const auto* held=input->find(id);if(!held || !held->owner.can_fire() || held->owner.rear==actor || held->owner.support!=hand::none)return false;
		const std::lock_guard lock(mutex);auto* value=find(id);const auto* cached=scenes.find(id);
		if(!value || !cached || value->view.fault || !value->view.definition || value->assembly!=held->assembly ||
			value->view.owner.rear_revision!=held->owner.rear_revision || value->view.reference_generation!=input->input.reference_generation ||
			(draw ? value->view.part_leased() : value->view.ammo.magazine_hand!=actor))return false;
		auto scene=*cached;scene.contact.knife_held=hi::has(actor,hi::domain::knife);
		if(!sample_contact(scene,value->view,*input) || scene.contact.knife_held)return false;
		const auto& p=*value->view.definition;
		if(scene.contact.waist_distance>p.interaction.waist_radius || scene.contact.magazine_pose>=p.interaction.magazine_pose_count)return false;
		const auto observed=native_ammunition::observe_owned(server_ps,id);if(!observed.valid || !reconcile(*value,observed))return false;
		auto request=mechanics::request{draw?mechanics::operation::draw_magazine:mechanics::operation::cancel_magazine,
			value->view.ammo.weapon,value->view.ammo.instance_generation,value->view.ammo.revision,held->owner.rear,actor};
		request.disposition=ammunition::disposition_reason::waist_return;
		const auto tx=mechanics::plan(p.ammunition,value->view.ammo,request);
		const auto expected=native_ammunition::observe_owned(server_ps,id);
		if(!tx || tx.before.loaded!=tx.after.loaded || !expected.valid ||
			ammunition::projection{expected.ammo.loaded,expected.ammo.reserve}!=tx.before || !write(expected.ammo,tx.after.reserve,context))return false;
		// Both native pools have committed. Publish the new escrow and seed the
		// existing gesture with this held Trigger, without replaying acquisition.
		++commits;value->view.ammo=tx.next;value->view.owner=held->owner;value->last_scene=scene;
		value->gesture.exchange_supply(p.interaction,scene.contact,input->input,held->owner,now,draw);
		value->view.magazine_seated=value->view.magazine_grabbed=value->view.knife_magazine_grasp=false;
		value->view.magazine_pose=value->gesture.magazine_pose();value->view.well_contact=false;
		value->view.requires_withdrawal=value->gesture.requires_withdrawal();
		value->view.reference_generation=input->input.reference_generation;value->view.input_sequence=input->input.sequence;
		value->view.decision=value->gesture.decision();return true;
	}
	bool insert_recovered(weapon_identity id,hand actor,int rounds,const reload_profile& source,std::uint8_t pose,const hands::anchor& world)noexcept
	{
		namespace hi=hand_interaction;
		const auto* input=hi::simulation();if(!active() || !input || !hi::has(actor,hi::domain::reload_item) || !valid_hand(actor))return false;
		const auto now=clock::now();if(!finite(world.position) || !finite_part_quat(world.rotation) || !input->input.focused ||
			now<input->input.sampled_at || now-input->input.sampled_at>150ms)return false;
		const auto* held=input->find(id);if(!held || !held->owner.can_fire() || held->owner.rear==actor || held->owner.support==actor)return false;
		const std::lock_guard lock(mutex);auto* value=find(id);const auto* cached=scenes.find(id);
		if(!value || !cached || value->view.fault || value->view.part_leased() || !value->view.definition ||
			now<value->gesture.insertion_after() || !reload_items::compatible(source,*value->view.definition))return false;
		const auto observed=native_ammunition::observe_owned(server_ps,id);if(!observed.valid || !reconcile(*value,observed))return false;
		auto scene=*cached;if(!sample_contact(scene,value->view,*input) || !input->input.trigger[int(actor)].down)return false;
		const auto& p=*value->view.definition;
		scene.contact.attached_magazine_pose=select_magazine_grip(p,{},int(actor),scene.binding.mirror,false,true,pose,std::nullopt,true).index;
		const auto tip=magazine_tip_in_well(p,held->gun,world,scene.units_per_meter);
		if(!sweep_well(tip,tip,p.interaction.well_radius+p.interaction.well_release_margin,
			p.interaction.well_contact_depth+p.interaction.well_release_margin,p.interaction.well_capture_below+p.interaction.well_release_margin) ||
			magazine_alignment(p,held->gun,world)<p.interaction.insertion_cosine)return false;
		scene.held_world=world;scene.contact.magazine_top_in_well=tip;value->last_scene=scene;value->view.owner=scene.owner;
		auto request=mechanics::request{mechanics::operation::insert_external_magazine,value->view.ammo.weapon,
			value->view.ammo.instance_generation,value->view.ammo.revision,scene.owner.rear,actor};request.external_rounds=rounds;
		const auto tx=mechanics::plan(p.ammunition,value->view.ammo,request);if(!tx || !commit(*value,tx))return false;
		value->view.ammo=tx.next;value->gesture.seat_external(scene.contact,input->input,scene.owner,now);
		value->view.magazine_seated=true;value->view.magazine_grabbed=false;value->view.magazine_pose=value->gesture.magazine_pose();
		value->view.knife_magazine_grasp=false;value->view.well_contact=value->view.requires_withdrawal=false;
		value->view.reference_generation=input->input.reference_generation;value->view.input_sequence=input->input.sequence;
		value->view.decision=value->gesture.decision();return true;
	}
	void update_lifecycle(bool suspended){tick(false,suspended);}
	void report_interactions()noexcept
	{
		namespace hi=hand_interaction;const std::lock_guard lock(mutex);
		for(const auto& i:inventory)if(i.view.active && carry::contains(i.view.owner.id()) && i.view.part_leased() && valid_hand(i.view.owner.holding_hand()))
		{
			const auto& v=i.view;const bool supply=v.magazine_leased();
			const auto pose=supply && v.knife_magazine_grasp?hi::recipe::knife_magazine:!supply && v.knife_slide_grasp?hi::recipe::knife_part:hi::recipe::single;
			hi::observed(hand(1-int(v.owner.holding_hand())),{hi::object(hi::domain::magazine,v.owner.id(),0,i.assembly),supply?hi::role::supply:hi::role::part,
				hi::button::trigger,pose,{}});
		}
	}
	presentation current(weapon_identity id) noexcept
	{
		const std::lock_guard lock(mutex);
		const auto* value = find(id);
		return value ? value->view : presentation{};
	}
	bool prepare_transfer(weapon_identity id,presentation& saved) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server)) return false;
		const std::lock_guard lock(mutex);
		auto* value=find(id); saved={};
		if (!value) return true;
		if (value->view.fault)
		{
			const auto observed=native_ammunition::observe_owned(game::g_entities[0].client,id);
			const auto cleanup=ammunition::release_fault({observed.ammo.loaded,observed.ammo.reserve},value->view.ammo.held_rounds);
			if (!observed.valid || !cleanup || !native_ammunition::commit_owned(observed.ammo,cleanup->loaded,cleanup->reserve)) return false;
			// Keep firing blocked; transfer only native counts after returning our
			// escrow once. A faulty chamber is not serialized into the world item.
			value->view.ammo.held_rounds=0;value->view.ammo.magazine_hand=hand::none;
			value->view.ammo.reserve_rounds=cleanup->reserve;++value->view.ammo.revision;
			value->gesture={};value->view.slide_held=value->view.magazine_seated=value->view.magazine_grabbed=false;
			value->view.belt_grip={};
			return true;
		}
		const auto current_ammo=native_ammunition::observe_carried(game::g_entities[0].client,id);
        if(!current_ammo.valid || current_ammo.loaded!=mechanics::native_ammo(value->view.ammo).loaded)return false;
        if(value->view.ammo.reserve_rounds!=current_ammo.reserve)
        {value->view.ammo.reserve_rounds=current_ammo.reserve;++value->view.ammo.revision;}
        if (!interrupt(*value)) return false;
		saved=value->view; return true;
	}
	bool restore_transfer(const presentation& saved) noexcept
	{
		if (!saved.active) return true;
		if (!scheduler::is_executing(scheduler::pipeline::server)) return false;
		const auto observed=native_ammunition::observe_owned(game::g_entities[0].client,saved.owner.id());
		const bool compatible=observed.valid && saved.definition && saved.definition->matches_native(observed.native_name.data(),observed.base_capacity) &&
			observed.ammo.loaded==mechanics::native_ammo(saved.ammo).loaded;
		const std::lock_guard lock(mutex);
		return runtime_lifecycle::restore_transfer(inventory,saved,compatible,[&](auto& value){value.native_name=observed.native_name;value.view.ammo.reserve_rounds=observed.ammo.reserve;++value.view.ammo.revision;});
	}
	bool support_available(const controller_input::frame& input,const hold& owner) noexcept
	{
		const std::lock_guard lock(mutex);
		const auto* value=find(owner.id());
		if (!value || !value->view.active) return true;
		const auto& state=value->view;
		return support_available(input,owner,state.belt_grip.part!=belt_feed::lease::none || state.slide_held || state.magazine_seated ||
			state.magazine_grabbed || state.ammo.magazine_hand!=hand::none);
	}
	bool part_leased(weapon_identity id) noexcept
	{
		const std::lock_guard lock(mutex);const auto* value=find(id);
		return value && value->view.active && value->view.part_leased();
	}
	void publish_scene(const scene_frame& value) noexcept
	{
		const auto ownership=carry::scene_ownership();
		const std::lock_guard lock(mutex);
		runtime_lifecycle::publish_scene(scenes,last_published_scene,value,ownership);
	}
	void set_boundary_ready(unsigned bit) noexcept { boundaries.fetch_or(bit); }
	bool blocks_reload(const void* ps, int side) noexcept
	{
		if (side || native_ammunition::local_role(ps) < 0) return false;
		const auto observed = native_ammunition::observe_reload(ps);
		const std::lock_guard lock(mutex);
		// Claim mapped feeds before scene admission too. A draw/model transition
		// must not let native auto-reload refill the gun while geometry is pending.
		return observed.valid && (find(observed.ammo.id()) ||
			(active() && native_reload_profile(observed.native_name.data(),observed.base_capacity)));
	}
	bool allow_fire(const void* ps, int command_time, int side) noexcept
	{
		const int role = native_ammunition::local_role(ps);
		if (side || role < 0) return true;
		const auto native = native_ammunition::observe_reload(ps);
		const auto& observed = native.ammo;
		const std::lock_guard lock(mutex);
		auto* value = observed.valid ? find(observed.id()) : nullptr;
		if (!value) return !active() || !native.valid || !native_reload_profile(native.native_name.data(),native.base_capacity);
		const auto& state = value->view.ammo;
		// PM_Weapon schedules native automatic rechambering after a bolt-action
		// shot. Manual feeds fire exclusively through allow_owned_shot/settle.
		if (value->view.definition->ammunition.feed==mechanics::feed_type::manual_bolt) return false;
		if (role == 0 && value->view.fault) return false;
		const bool allowed = value->view.owner.can_fire() && !value->view.fault && observed.loaded == mechanics::native_ammo(state).loaded &&
			mechanics::ready(value->view.definition->ammunition, state, value->view.belt_grip.part!=belt_feed::lease::none || value->view.slide_held || value->view.magazine_grabbed);
		// A prediction running before the server may use only an exact current
		// count match. Historical replays use the decision above, not today's chamber.
		return history.allow(state.weapon,state.instance_generation,command_time,observed.loaded,role == 0,allowed);
	}
	bool allow_owned_shot(const hold& owner,const native_ammunition::snapshot& observed) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || !observed.valid || observed.id()!=owner.id() || !owner.can_fire()) return false;
		const auto native=native_ammunition::observe_owned(game::g_entities[0].client,owner.id());
		const std::lock_guard lock(mutex);auto* value=find(owner.id());
		if (!value) return !active() || !native.valid || !native_reload_profile(native.native_name.data(),native.base_capacity);
		if (value->view.fault || !value->view.definition || observed.loaded!=mechanics::native_ammo(value->view.ammo).loaded ||
			!mechanics::ready(value->view.definition->ammunition,value->view.ammo,value->view.belt_grip.part!=belt_feed::lease::none || value->view.slide_held || value->view.magazine_grabbed)) return false;
		value->view.owner=owner;return true;
	}
	void consumed(const void* ps, int command_time, std::uint32_t weapon, bool alternate, int amount, int side,
		const native_ammunition::snapshot& before, const native_ammunition::snapshot& after,bool sustained) noexcept
	{
		if (side || alternate || native_ammunition::local_role(ps) != 0 || amount != 1 || !before.valid || !after.valid || before.id()!=after.id() ||
			before.weapon != weapon || after.weapon != weapon || after.loaded != before.loaded - int(!sustained) || after.reserve != before.reserve) return;
		const std::lock_guard lock(mutex);
		auto* value = find(before.id());
		if (!value || value->view.fault) return;
		auto& state = value->view.ammo;
		auto candidate = state;
		// Pickups/shared-pool changes can precede a shot within G_RunFrame,
		// before the scheduler's reconciliation. Trust this native BEFORE pool,
		// but never reconstruct the chamber from a changed loaded total.
		candidate.reserve_rounds = before.reserve;
		mechanics::request request{mechanics::operation::accepted_shot, weapon, candidate.instance_generation, candidate.revision, value->view.owner.rear, value->view.owner.rear};
		request.sustained=sustained;
		const auto tx = mechanics::plan(value->view.definition->ammunition,candidate,request);
		if (!tx || tx.before.loaded != before.loaded || tx.before.reserve != before.reserve || tx.after.loaded != after.loaded)
		{ value->view.fault = true; ++failures; reason = "native shot mismatch; instance blocked"; return; }
		state = tx.next; ++shots; // engine ALREADY spent; never call commit_owned here
		value->view.shot_at = clock::now();
		history.spent(weapon,state.instance_generation,command_time);
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			enabled = dvars::register_bool("vr_physicalReload", true, game::DVAR_FLAG_SAVED,
				"Authored weapon physical magazine/chamber/slide interaction; intermediate saves unsupported");
			command::add("vr_reload_interaction_status", [] {
				// Snapshot under the gameplay lock; formatting/console/I/O must not
				// block scene publication or the authoritative simulation callback.
				decltype(inventory) instances;
				scene_frame sampled_scene;
				native_ammunition::reload_snapshot native;
				std::uint64_t ticks{}, committed{}, failed{}, fired{}, granted{};
				unsigned contracts{}; bool is_active{}; const char* why{};
				{
					const std::lock_guard lock(mutex);
					instances = inventory; sampled_scene = last_published_scene; native = latest_native;
					ticks = server_ticks; committed = commits; failed = failures; fired = shots;
					granted = native_grants;
					contracts = boundaries.load(); is_active = active(); why = reason;
				}
				std::ostringstream out;
				out << scene_models::runtime_model_status();
				{
					out << "[VR physical reload] contracts=" << contracts << " enabled=" << is_active
						<< " server_ticks=" << ticks << " commits=" << committed << " failures=" << failed
						<< " shots=" << fired << " native_grants=" << granted << " reason=" << why << '\n';
					out << "native_valid=" << native.valid << " weapon=" << native.ammo.weapon
						<< " native_name=" << (native.valid ? native.native_name.data() : "unavailable") << " capacity=" << native.base_capacity
						<< " loaded=" << native.ammo.loaded << " reserve=" << native.ammo.reserve
						<< " state=" << native.ammo.weapon_state << " scene_valid=" << sampled_scene.contact.valid
						<< " scene_weapon=" << sampled_scene.owner.weapon << " scene_gameplay=" << sampled_scene.gameplay
						<< " scene_manipulation=" << sampled_scene.manipulation
						<< " scene_knife=" << sampled_scene.contact.knife_held
						<< " input_sequence=" << sampled_scene.input.sequence << " focused=" << sampled_scene.input.focused
						<< " scene_age_ms=" << std::chrono::duration<double,std::milli>(clock::now()-sampled_scene.input.sampled_at).count()
						<< " secondary_active=" << sampled_scene.input.secondary[0].active << '/' << sampled_scene.input.secondary[1].active
						<< " trigger_active=" << sampled_scene.input.trigger[0].active << '/' << sampled_scene.input.trigger[1].active << '\n';
					for (const auto& i : instances) if (i.view.active)
					{
						const auto& s = i.view.ammo;
						out << "weapon=" << s.weapon << " instance=" << s.instance_generation << " fault=" << i.view.fault
							<< " profile=" << (i.view.definition ? i.view.definition->id : "none")
							<< " magazine=" << s.magazine_inserted << ':' << s.magazine_rounds << " chamber=" << s.chamber_loaded
							<< " action=" << int(s.action) << " reserve=" << s.reserve_rounds << " held=" << s.held_rounds
							<< " hand=" << int(i.view.owner.holding_hand()) << " rear_revision=" << i.view.owner.rear_revision
							<< " scene_sequence=" << i.last_scene.input.sequence << " manipulation=" << i.last_scene.manipulation
							<< " decision=" << i.view.decision << " slide=" << i.view.slide_held << ':' << i.view.slide_travel
							<< " cover=" << s.belt.cover << " bridge=" << s.belt.bridge << " belt_laid=" << s.belt.laid << " belt_grip=" << int(i.view.belt_grip.part)
							<< " belt_contact=" << i.view.examined.belt.cover_distance << ':' << i.view.examined.belt.chain_distance << ':' << i.view.examined.belt.feed_distance
							<< " cover_push=" << i.gesture.belt_push_reason() << " palm_valid=" << i.view.examined.belt.push.valid
							<< " palm_hinge_m=" << i.view.examined.belt.push.point[0] << ',' << i.view.examined.belt.push.point[1] << ',' << i.view.examined.belt.push.point[2]
							<< " palm_hinge_normal=" << i.view.examined.belt.push.palm[0] << ',' << i.view.examined.belt.push.palm[1] << ',' << i.view.examined.belt.push.palm[2]
							<< " squeeze_active=" << i.last_scene.input.squeeze[0].active << '/' << i.last_scene.input.squeeze[1].active
							<< " squeeze_down=" << i.last_scene.input.squeeze[0].down << '/' << i.last_scene.input.squeeze[1].down
							<< " bridge_contact=" << i.view.examined.belt.bridge_distance
							<< " bridge_release_contact=" << i.view.examined.belt.release_distance << " bridge_settled=" << i.view.examined.belt.bridge_settled
							<< " slide_grip_pose=" << static_cast<unsigned>(i.view.slide_grip.pose)
							<< " magazine_pose=" << static_cast<unsigned>(i.view.magazine_pose)
							<< " magazine_candidate=" << static_cast<unsigned>(i.view.examined.magazine_pose)
							<< " attached_magazine_candidate=" << static_cast<unsigned>(i.view.examined.attached_magazine_pose)
							<< " magazine_pose_count=" << i.view.definition->magazine_grasps.size()
							<< " seated_hand=" << i.view.magazine_seated << " pulling_magazine=" << i.view.magazine_grabbed << '\n';
						out << "knife_magazine_capable=" << bool(i.view.definition->knife_magazine_in_wrist)
							<< " knife_magazine_grasp=" << (i.view.magazine_leased() && i.view.knife_magazine_grasp)
							<< " knife_slide_capable=" << !i.view.definition->knife_slide_grips.empty()
							<< " knife_slide_grasp=" << (i.view.slide_held && i.view.knife_slide_grasp) << '\n';
						if (i.view.definition->interaction.manual_bolt)
						out << "manual_bolt actor=either lift=" << s.bolt.lift << " travel=" << s.bolt.travel
								<< " case=" << s.bolt.spent_case << " feeding=" << s.bolt.feeding << " cocked=" << s.bolt.cocked
								<< " feed_armed=" << s.bolt.feed_armed << " ready=" << mechanics::ready(i.view.definition->ammunition,s,i.view.slide_held) << '\n';
						out << "effects(out/draw/in/cancel/close)=" << i.effects[1] << '/' << i.effects[2] << '/'
							<< i.effects[3] << '/' << i.effects[4] << '/' << i.effects[5]
							<< " take=" << i.effects[static_cast<size_t>(mechanics::effect::magazine_take)] << '\n';
						for (auto n=i.attempt_count > i.attempts.size() ? i.attempt_count-i.attempts.size()+1 : 1; n<=i.attempt_count; ++n)
						{
							const auto& a=i.attempts[n%i.attempts.size()]; const auto& g=a.contact;
							out << "attempt=" << n << " seq=" << g.input_sequence << " decision=" << a.decision
								<< " waist_m=" << g.waist_distance << " slide_m=" << g.slide_distance << " alignment=" << g.insertion_alignment
								<< " slide_grip_candidate=" << static_cast<unsigned>(g.slide_pose)
								<< " tip_m=" << g.magazine_top_in_well[0] << ',' << g.magazine_top_in_well[1] << ',' << g.magazine_top_in_well[2]
								<< " held=" << a.held << " trigger/squeeze=" << a.trigger << '/' << a.squeeze << " slide=" << a.slide << ':' << a.travel;
							if (i.view.definition->interaction.manual_magazine)
								out << " mag_contact=" << g.magazine.valid << " mag_grip_m=" << g.magazine.grip_distance
									<< " strike_boxes=" << bool(g.magazine.strike) << " end_distance_m="
									<< (g.magazine.strike ? closest_box(*g.magazine.strike,0).distance : -1) << ','
									<< (g.magazine.strike ? closest_box(*g.magazine.strike,1).distance : -1);
							out << '\n';
						}
					}
				}
				out << presentation_status();
				const auto text = out.str();
				// A full 32-attempt ring exceeds console::print's 4 KiB CRT buffer.
				// Existing chunked output retains every line without invoking the
				// invalid-parameter termination path of _vsnprintf_s.
				console::print_text(console::con_type_info, text);
				scheduler::once([text] {
					if (!utils::io::write_file_atomic("minidumps/h2-mod-vr-physical-reload.txt", text))
						console::warn("[VR physical reload] status file write failed\n");
				}, scheduler::pipeline::async);
			});
			// The hand interaction coordinator owns the simulation update order.
		}
		void pre_destroy() override { alive = false; }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::physical_reload::component)
