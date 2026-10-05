#include <std_include.hpp>
#include "hand_interaction/runtime.hpp"
#include "hand_interaction/mechanical_contacts.hpp"
#include "native_scripted_control.hpp"
#include "break_action_runtime.hpp"
#include "quick_reload_runtime.hpp"
#include "break_action_presenter.hpp"
#include "native_shot_history.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapon_runtime_lifecycle.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_interaction.hpp"
#include "weapon_feedback.hpp"
#include "break_action_profiles.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <mutex>
#include <sstream>

namespace vr::gameplay::weapons::break_action
{
	namespace
	{
		std::mutex mutex;
		std::atomic_uint boundaries{};
		std::atomic_bool alive{true};
		game::dvar_t* enabled{};
		scene_frame last_published_scene{}; // Diagnostics never drive mechanics.
		instance_cache<scene_frame,carry::inventory::capacity> scenes;
		struct instance { presentation view{}; controller gesture{}; std::uint64_t assembly{}; scene_frame last_scene{}; };
		std::array<instance,15> inventory{};
		native_shot_history history{};
		const void* server_ps{}; int game_time{};
		std::uint64_t server_timeline{};
		bool script_suspended{};
		std::uint64_t generation{1}, commits{}, failures{}, shots{};
		const char* reason{"waiting for break_action presentation contract"};
		instance* find(weapon_identity id) noexcept
		{ for (auto& i : inventory) if (i.view.active && i.view.owner.id() == id) return &i; return nullptr; }
		bool active() noexcept { return alive.load() && boundaries.load()==7 && enabled && enabled->current.enabled; }
		bool commit(instance& i,const transaction& tx,bool direct_load=false)
		{
			if(!native_ammunition::compare_commit_owned(server_ps,i.view.owner.id(),tx.before,tx.after))
			{++failures;reason="native compare rejected; state and escrow retained";return false;}
			++commits;if(tx.feedback==effect::none || tx.silent)return true;
			auto& v=i.view;const auto& scene=i.last_scene;const auto now=clock::now();
			++v.event_sequence;
			auto chambers=scene.chambers_world;
			if(tx.ejected)for(unsigned n=0;n<v.definition->ammunition.capacity;++n)
				chambers[n]=hands::pose_math::compose(hands::pose_math::compose(scene.gun_world,barrel_pose(*v.definition,1)),v.definition->chamber_in_barrel[n]);
			v.events[v.event_sequence%v.events.size()]={v.event_sequence,tx.feedback,now,direct_load?chambers[0]:scene.shell_world,scene.units,tx.ejected,tx.chamber,chambers};
			if(scene.owner.id()==v.owner.id() && scene.gameplay && now>=scene.input.sampled_at && now-scene.input.sampled_at<=100ms)
			{
				const auto kind=tx.feedback==effect::open ? mechanics::effect::action_rear : tx.feedback==effect::close ? mechanics::effect::action_close :
					tx.feedback==effect::draw ? mechanics::effect::magazine_draw : tx.feedback==effect::load ? mechanics::effect::magazine_in : mechanics::effect::none;
				feedback::event event{kind,v.owner,scene.input.reference_generation,now,scene.chambers_world[0].position};
				event.explicit_sound=v.definition->sound_key(tx.feedback);event.break_definition=v.definition;event.mechanical_instance=v.ammo.instance_generation;
				feedback::publish(event);
			}
			return true;
		}
		bool interrupt(instance& i)
		{
			i.view.quick_load.reset();
			i.view.fire_armed=false;
			i.view.barrel_held=false;
			return i.gesture.interrupt(i.view.definition->ammunition,i.view.ammo,i.view.owner.holding_hand(),
				[&](const auto& tx) { return commit(i,tx); });
		}
		bool reconcile(instance& i, const native_ammunition::reload_snapshot& observed)
		{
			auto& s=i.view.ammo;
			if (i.view.fault) return false;
			if (!i.view.definition->matches_native(observed.native_name.data(),observed.base_capacity))
			{ i.view.fault=true; reason="owned break_action definition changed"; return false; }
			if (observed.ammo.loaded!=native_ammo(s).loaded)
			{
				// A native box/pickup may credit the clip. Preserve physical contents
				// and opening; move its positive budget into reserve, not a fake load.
				const auto capacity=int(i.view.definition->ammunition.capacity);
				const auto credited=ammunition::granted_reserve(native_ammo(s),{observed.ammo.loaded,observed.ammo.reserve},capacity,capacity,1000000);
				if (!credited || s.revision==UINT64_MAX)
				{ i.view.fault=true; ++failures; reason="unexplained native break_action count mutation"; return false; }
				if (!native_ammunition::commit_owned(observed.ammo,native_ammo(s).loaded,*credited))
				{ ++failures; reason="native grant compare rejected"; return false; }
				s.reserve=*credited; ++s.revision;
			}
			else if (observed.ammo.reserve!=s.reserve)
			{
				if (s.revision==UINT64_MAX) { i.view.fault=true; return false; }
				s.reserve=observed.ammo.reserve; ++s.revision;
			}
			return true;
		}
		void update_instance(const void* ps, scene_frame scene, const native_ammunition::reload_snapshot& observed, clock::time_point now)
		{
			auto* i=find(observed.ammo.id());
			const bool bootstrap=!i;
			if (i && !reconcile(*i,observed)) return;
			if (!i)
			{
				const auto* p=native_break_action_profile(observed.native_name.data(),observed.base_capacity,scene.definition);
				if (!p) return;
				if (scene.definition!=p || !scene.contact.valid || !scene.assembly || !scene.gameplay ||
					scene.owner.id()!=observed.ammo.id() || !valid_hand(scene.owner.holding_hand()) ||
					now<scene.input.sampled_at || now-scene.input.sampled_at>150ms || (!carry::active() && observed.ammo.weapon_state!=0))
				{ reason="waiting for authored tracked break_action scene and native idle"; return; }
				if (utils::hook::invoke<int>(0x1406A3A60,ps,observed.ammo.weapon,false)!=int(p->ammunition.capacity))
				{ reason="modified native capacity rejected"; return; }
				const auto imported=import_native(p->ammunition,observed.ammo.weapon,++generation,
					{observed.ammo.loaded,observed.ammo.reserve});
				if (!valid(p->ammunition,imported)) return;
				for (auto& slot : inventory) if (!slot.view.active) { i=&slot; break; }
				if (!i) { reason="break_action inventory full"; return; }
				*i={}; i->view.active=true; i->view.ammo=imported; i->view.definition=p;
				i->view.owner=scene.owner; i->assembly=scene.assembly;
			}
			if (i->view.fault) return;
			if (bootstrap)
			{
				// Re-sample the accepted first edge with its actual mechanical
				// generation; no render-frame acknowledgement is required.
				const auto* input=hand_interaction::simulation();
				if (!input || !hand_interaction::sample(scene,i->view,*input)) return;
			}
			if (i->assembly!=scene.assembly) { if (!interrupt(*i)) return; i->assembly=scene.assembly; }
			if (scene.owner.id()==i->view.owner.id()) i->view.owner=scene.owner;
			auto g=scene.contact;
			if (scene.definition!=i->view.definition) g.valid=false;
			i->last_scene=scene;
			i->gesture.update(i->view.definition->interaction,i->view.definition->ammunition,i->view.ammo,
				scene.input,i->view.owner,g,scene.gameplay && scene.owner.id()==i->view.owner.id(),now,
				[&](const auto& tx) { return commit(*i,tx); }, {scene.manipulation,
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::hinge,scene.owner.id()),
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::hinge,scene.owner.id(),hand_interaction::button::trigger,hand_interaction::role::supply),
				hand_interaction::input(hand(1-int(scene.owner.holding_hand())),hand_interaction::button::trigger).press,
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::hinge,scene.owner.id(),hand_interaction::button::trigger,hand_interaction::role::part),hand_interaction::input(hand(1-int(scene.owner.holding_hand())),hand_interaction::button::trigger).release});
			i->view.fire_armed=i->gesture.fire_armed(); i->view.barrel_held=i->gesture.barrel_held();
			i->view.sampled_at=scene.input.sampled_at; i->view.reference=scene.input.reference_generation;
			i->view.input_sequence=scene.input.sequence; reason=i->gesture.decision();
			const auto& ammo=i->view.ammo;
			const auto quick=plan(i->view.definition->ammunition,ammo,{operation::quick_load,ammo.weapon,
				ammo.instance_generation,ammo.revision,i->view.owner.holding_hand(),i->view.owner.holding_hand()});
			if(quick_reload::ready(i->view.quick_load,i->view.owner,ammo.instance_generation,scene.assembly,i->view.definition->id,
				scene.gameplay && scene.definition==i->view.definition && bool(quick),now) && commit(*i,quick,true))
			{i->view.ammo=quick.next;i->view.quick_load.reset();}
		}
		void tick(bool interactions,bool suspended=false)
		{
			if (!alive.load() || !scheduler::is_executing(scheduler::pipeline::server)) return;
			const bool gameplay=game::CL_IsCgameInitialized();
			const auto* ps=gameplay ? game::g_entities[0].client : nullptr;
			const int time=gameplay ? game::CG_GetGameTime(0) : 0;
			const auto observed=ps ? native_ammunition::observe_reload(ps) : native_ammunition::reload_snapshot{};
			const auto now=clock::now();
			std::array<hold,2> owners{};
			if (carry::active()) { const auto held=carry::held_instances(); for (size_t h=0;h<held.size();++h) owners[h]=held[h].owner; }
			else owners[0]=current_hold();
			const std::lock_guard lock(mutex);
			const auto timeline=native_ammunition::timeline();
			if (!ps || ps!=server_ps || time<game_time || timeline!=server_timeline)
			{ inventory={}; scenes={}; last_published_scene={}; history={}; ++generation; server_ps=ps; server_timeline=timeline; script_suspended=false; }
			game_time=time;
			if (!ps) { reason="waiting for local server player"; return; }
			if (!scripted_control::allowed(ps))
			{
				if (!script_suspended)
				{
					for (auto& i : inventory){i.gesture={};i.view.quick_load.reset();i.view.fire_armed=false;i.view.barrel_held=false;}
					scenes={};last_published_scene={};script_suspended=true;
				}
				reason="native script owns weapons";return;
			}
			script_suspended=false;
			for (auto& i : inventory)
			{
				if (!i.view.active) continue;
				const auto projected=carry::native_identity(i.view.owner.weapon);
				if (projection_rebind(i.view.owner.id(),projected) &&
					(projected.generation || native_ammunition::projected_identity(projected.weapon)==i.view.owner.id()))
				{
					const auto previous=i.view.owner.instance_generation;
					i.view.owner.instance_generation=projected.generation;
					if (!interrupt(i)) {i.view.owner.instance_generation=previous;continue;}
				}
				const auto owned=native_ammunition::observe_owned(ps,i.view.owner.id());
				if (!owned.valid) { i={}; continue; }
				if (!active() || !carry::active())
				{
					const auto refund=i.view.ammo.held_rounds;
					if (refund && !native_ammunition::commit_owned(owned.ammo,owned.ammo.loaded,owned.ammo.reserve+refund))
					{ ++failures; continue; }
					i={}; continue;
				}
				if (!reconcile(i,owned)) continue;
				const auto live=std::find_if(owners.begin(),owners.end(),[&](const hold& h){return h.id()==i.view.owner.id();});
				if (suspended || live==owners.end() || live->holding_hand()!=i.view.owner.holding_hand() || live->rear_revision!=i.view.owner.rear_revision)
					if (!interrupt(i)) continue; // Keep failed escrow and continue the bounded inventory pass.
			}
			if (!interactions) return;
			if (!active()) return;
			for (const auto& owner : owners)
			{
				const auto* cached=scenes.find(owner.id());
				if (!cached) continue;
				auto frame=*cached;
				if(const auto* input=hand_interaction::simulation())
				{
					const auto* value=find(owner.id());const auto view=value?value->view:presentation{};
					if(!hand_interaction::sample(frame,view,*input)){frame.input=input->input;frame.owner=owner;frame.contact.valid=false;frame.gameplay=true;}
					frame.manipulation=hand_interaction::permits(hand(1-int(owner.holding_hand())),hand_interaction::domain::hinge,owner.id());
				}
				if (frame.owner.rear_revision!=owner.rear_revision || frame.owner.holding_hand()!=owner.holding_hand()) continue;
				const auto owned=native_ammunition::observe_owned(ps,owner.id());
				if (owned.valid) update_instance(ps,frame,owned,now);
			}
		}
	}
	void collect_interactions(const hand_interaction::frame& input)noexcept
	{
		namespace hi=hand_interaction;const std::lock_guard lock(mutex);
		for(const auto& e:scenes.entries())if(e.id)
		{
			auto s=e.value;const auto* live=input.find(e.id);if(!live || !valid_hand(live->owner.holding_hand()))continue;
			const auto actor=hand(1-int(live->owner.holding_hand()));const auto edge=hi::input(actor,hi::button::trigger);
			if(!edge.press || !edge.down)continue;
			const auto* value=find(e.id);const auto view=value?value->view:presentation{};if(!hi::sample(s,view,input))continue;
			const float part=barrel_acquirable(s.definition->interaction,view.ammo,s.contact)?
				s.contact.barrel_distance/s.definition->interaction.barrel_radius:INFINITY;
			const bool supply=part>1 && s.contact.waist_distance<=s.definition->interaction.waist_radius;
			const float distance=supply?s.contact.waist_distance/s.definition->interaction.waist_radius:part;
			if(!std::isfinite(distance) || distance>1)continue;
			hi::offer({actor,{hi::object(hi::domain::hinge,e.id,0,s.assembly),supply?hi::role::supply:hi::role::part,hi::button::trigger,hi::recipe::single,{}},edge.event,supply?30u:20u,distance,1,true,true});
		}
	}
	void update_interactions(){tick(true);}
	void update_lifecycle(bool suspended){tick(false,suspended);}
	void report_interactions()noexcept
	{
		namespace hi=hand_interaction;const std::lock_guard lock(mutex);
		for(const auto& i:inventory){const auto& v=i.view;if(v.active && carry::contains(v.owner.id()) && valid_hand(v.owner.holding_hand()) && (v.ammo.loader_hand!=hand::none || v.barrel_held))
			hi::observed(hand(1-int(v.owner.holding_hand())),{hi::object(hi::domain::hinge,v.owner.id(),0,i.assembly),v.ammo.loader_hand!=hand::none?hi::role::supply:hi::role::part,hi::button::trigger,hi::recipe::single,{}});}
	}
	presentation current(weapon_identity id) noexcept
	{ const std::lock_guard lock(mutex); const auto* i=find(id); return i ? i->view : presentation{}; }
	bool allows_trigger(const hold& owner,std::uint64_t reference,clock::time_point now)noexcept
	{const std::lock_guard lock(mutex);const auto* i=find(owner.id());return !i || i->view.allows_trigger(owner,reference,now);}
	bool prepare_transfer(weapon_identity id,presentation& saved) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server)) return false;
		const std::lock_guard lock(mutex); auto* i=find(id); saved={};
		if (!i) return true;
		if (i->view.fault)
		{
			const auto observed=native_ammunition::observe_owned(game::g_entities[0].client,id);
			const auto cleanup=ammunition::release_fault({observed.ammo.loaded,observed.ammo.reserve},i->view.ammo.held_rounds);
			if (!observed.valid || !cleanup || !native_ammunition::commit_owned(observed.ammo,cleanup->loaded,cleanup->reserve)) return false;
			i->view.ammo.held_rounds=0;i->view.ammo.loader_hand=hand::none;
			i->view.ammo.reserve=cleanup->reserve;++i->view.ammo.revision;
			return true; // Native drop remains possible; never persist a guessed break_action.
		}
		const auto current_ammo=native_ammunition::observe_carried(game::g_entities[0].client,id);
        if(!current_ammo.valid || current_ammo.loaded!=native_ammo(i->view.ammo).loaded)return false;
        if(i->view.ammo.reserve!=current_ammo.reserve){i->view.ammo.reserve=current_ammo.reserve;++i->view.ammo.revision;}
        if (!interrupt(*i)) return false;
		saved=i->view; return true;
	}
	bool restore_transfer(const presentation& saved) noexcept
	{
		if (!saved.active) return true;
		if (!scheduler::is_executing(scheduler::pipeline::server)) return false;
		const auto observed=native_ammunition::observe_owned(game::g_entities[0].client,saved.owner.id());
		const bool compatible=observed.valid && saved.definition && saved.definition->matches_native(observed.native_name.data(),observed.base_capacity) &&
			observed.ammo.loaded==native_ammo(saved.ammo).loaded;
		const std::lock_guard lock(mutex);
		return runtime_lifecycle::restore_transfer(inventory,saved,compatible,[&](auto& value){value.view.ammo.reserve=observed.ammo.reserve;++value.view.ammo.revision;});
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
		if (side || native_ammunition::local_role(ps)<0) return false;
		const auto observed=native_ammunition::observe_reload(ps);
		const std::lock_guard lock(mutex); return observed.valid && (find(observed.ammo.id()) ||
			(active() && native_break_action_profile(observed.native_name.data(),observed.base_capacity)));
	}
	bool allow_fire(const void* ps, int command, int side) noexcept
	{
		const int role=native_ammunition::local_role(ps);
		if (side || role<0) return true;
		const auto native=native_ammunition::observe_reload(ps);
		const auto& observed=native.ammo;
		const std::lock_guard lock(mutex);
		auto* i=observed.valid ? find(observed.id()) : nullptr;
		if (!i) return !active() || !native.valid || !native_break_action_profile(native.native_name.data(),native.base_capacity);
		const auto& s=i->view.ammo; const auto now=clock::now();
		const bool allowed=i->view.owner.can_fire() && !i->view.fault && !i->view.barrel_held && i->view.fire_armed && now>=i->view.sampled_at &&
			now-i->view.sampled_at<=150ms && observed.loaded==native_ammo(s).loaded && ready(i->view.definition->ammunition,s);
		return history.allow(s.weapon,s.instance_generation,command,observed.loaded,role==0,allowed);
	}
	bool allow_owned_shot(const hold& owner,const native_ammunition::snapshot& observed) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || !observed.valid || observed.id()!=owner.id() || !owner.can_fire()) return false;
		const auto native=native_ammunition::observe_owned(game::g_entities[0].client,owner.id());
		const std::lock_guard lock(mutex);auto* i=find(owner.id());
		if (!i) return !active() || !native.valid || !native_break_action_profile(native.native_name.data(),native.base_capacity);
		// A trigger held through closure must not become a queued shot.
		// Keep this feed's neutral-rearm latch as well as the independent clock.
		if (i->view.fault || !i->view.definition || i->view.barrel_held ||
			!i->view.fire_armed || observed.loaded!=native_ammo(i->view.ammo).loaded || !ready(i->view.definition->ammunition,i->view.ammo)) return false;
		i->view.owner=owner;return true;
	}
	void consumed(const void* ps, int command, std::uint32_t weapon, bool alternate, int amount, int side,
		const native_ammunition::snapshot& before, const native_ammunition::snapshot& after,bool sustained) noexcept
	{
		if (side || alternate || amount!=1 || native_ammunition::local_role(ps)!=0 || !before.valid || !after.valid || before.id()!=after.id() ||
			before.weapon!=weapon || after.weapon!=weapon || after.loaded!=before.loaded-int(!sustained) || after.reserve!=before.reserve) return;
		const std::lock_guard lock(mutex); auto* i=find(before.id());
		if (!i || i->view.fault) return;
		const auto& s=i->view.ammo;
		request q{operation::accepted_shot,weapon,s.instance_generation,s.revision,i->view.owner.rear,i->view.owner.rear};q.sustained=sustained;
		const auto tx=plan(i->view.definition->ammunition,s,q);
		if (!tx || tx.before!=ammunition::projection{before.loaded,before.reserve} || tx.after!=ammunition::projection{after.loaded,after.reserve})
		{ i->view.fault=true; ++failures; reason="accepted shot projection mismatch"; return; }
		i->view.ammo=tx.next; i->view.shot_at=clock::now(); ++shots;
		history.spent(weapon,s.instance_generation,command);
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			enabled=dvars::register_bool("vr_breakActionReload",true,game::DVAR_FLAG_SAVED,"Physical break-action chambers and hinge interaction");
			command::add("vr_break_action_status",[] {scheduler::once([] {
				presentation v;scene_frame scene;const char* why;
				{const std::lock_guard lock(mutex);scene=last_published_scene;auto* i=find(scene.owner.id());v=i ? i->view : presentation{};why=reason;}
				const auto native=native_ammunition::observe_reload(game::CL_IsCgameInitialized() ? game::g_entities[0].client : nullptr);
				console::info("[VR break action] contracts=%u active=%d fault=%d profile=%s live/spent=%u/%u reserve/held=%d/%d phase=%d hinge=%.3f barrel_held=%d reason=%s\n",
					boundaries.load(),v.active,v.fault,v.definition ? v.definition->id.data() : "none",v.ammo.live,v.ammo.spent,v.ammo.reserve,v.ammo.held_rounds,int(v.ammo.phase),v.ammo.hinge,v.barrel_held,why);
				const auto& g=scene.contact;
				const auto text=std::format("native={} name={} capacity={} profile={} active={} fault={} live_mask={} spent_mask={} reserve={} held={} hinge={:.3f} barrel_held={} fire_armed={} reason={}\nscene={} manipulation={} waist={:.3f} barrel_distance={:.3f} barrel_angle={:.3f} contact0={:.3f},{:.3f},{:.3f} alignment0={:.3f} contact1={:.3f},{:.3f},{:.3f} alignment1={:.3f}\n",
					native.valid,native.native_name.data(),native.base_capacity,v.definition ? v.definition->id : "none",v.active,v.fault,v.ammo.live,v.ammo.spent,v.ammo.reserve,v.ammo.held_rounds,v.ammo.hinge,v.barrel_held,v.fire_armed,why,
					g.valid,scene.manipulation,g.waist_distance,g.barrel_distance,g.barrel_angle,g.shell_in_chamber[0][0],g.shell_in_chamber[0][1],g.shell_in_chamber[0][2],g.alignment[0],g.shell_in_chamber[1][0],g.shell_in_chamber[1][1],g.shell_in_chamber[1][2],g.alignment[1]);
				utils::io::write_file("minidumps/h2-mod-vr-break-action.txt",text);
			},scheduler::pipeline::server);});
			// Updated by the single hand-interaction coordinator.
		}
		void pre_destroy() override { alive=false; }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::break_action::component)
