#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "hand_interaction/runtime.hpp"
#include "hand_interaction/mechanical_contacts.hpp"
#include "native_scripted_control.hpp"
#include "tube_runtime.hpp"
#include "tube_presenter.hpp"
#include "native_shot_history.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapon_runtime_lifecycle.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_interaction.hpp"
#include "weapon_feedback.hpp"
#include "tube_profiles.hpp"
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

namespace vr::gameplay::weapons::tube
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
		const char* reason{"waiting for tube presentation contract"};
		instance* find(weapon_identity id) noexcept
		{ for (auto& i : inventory) if (i.view.active && i.view.owner.id() == id) return &i; return nullptr; }
		bool active() noexcept { return alive.load() && boundaries.load()==7 && enabled && enabled->current.enabled; }
		bool commit(instance& i, const transaction& tx)
		{
			if (!native_ammunition::compare_commit_owned(server_ps,i.view.owner.id(),tx.before,tx.after))
			{ ++failures; reason="native compare rejected; state and escrow retained"; return false; }
			++commits;
			if (tx.feedback==effect::none || tx.silent) return true;
			auto& v=i.view;
			const auto& scene=i.last_scene;
			const auto now=clock::now();
			const bool ejected=tx.feedback==effect::rack_open && tx.ejected;
			const auto world=ejected ? scene.ejection_world : scene.shell_world;
			++v.event_sequence;
			v.events[v.event_sequence%v.events.size()]={v.event_sequence,tx.feedback,now,world,scene.units,
				ejected ? tx.ejected : v.ammo.held_rounds};
			if(tx.feedback==effect::load_tube || tx.feedback==effect::load_port) v.load_at=now;
			if(scene.owner.id()==v.owner.id() && scene.gameplay && now>=scene.input.sampled_at && now-scene.input.sampled_at<=100ms)
			{
				const auto kind=tx.case_ejected ? mechanics::effect::case_eject : tx.feedback==effect::draw ? mechanics::effect::magazine_draw :
					(tx.feedback==effect::load_tube || tx.feedback==effect::load_port) ? mechanics::effect::magazine_in :
					tx.feedback==effect::rack_close ? mechanics::effect::action_close : mechanics::effect::none;
				feedback::event event{kind,v.owner,scene.input.reference_generation,now,world.position};
				event.explicit_sound=v.definition->sound_key(tx.feedback);event.tube_definition=v.definition;
				event.mechanical_instance=v.ammo.instance_generation;
				if(tx.case_ejected){event.has_brass=true;event.brass=scene.ejection_world;event.brass.position=hands::add(event.brass.position,hands::rotate(event.brass.rotation,hands::scale(v.definition->shell_center,scene.units*.0254f)));}
				feedback::publish(event);
			}
			return true;
		}
		bool interrupt(instance& i)
		{
			i.view.fire_armed=false;
			i.view.rack_held=false;
			const bool result=i.gesture.interrupt(i.view.definition->ammunition,i.view.ammo,i.view.owner.holding_hand(),
				[&](const auto& tx) { return commit(i,tx); });
			i.view.lever=i.gesture.lever_pose();return result;
		}
		bool reconcile(instance& i, const native_ammunition::reload_snapshot& observed)
		{
			auto& s=i.view.ammo;
			if (i.view.fault) return false;
			if (!i.view.definition->matches_native(observed.native_name.data(),observed.base_capacity))
			{ i.view.fault=true; reason="owned tube definition changed"; return false; }
			if (observed.ammo.loaded!=native_ammo(s).loaded)
			{
				// A native box/pickup may credit the clip. Preserve physical contents
				// and opening; move its positive budget into reserve, not a fake load.
				const auto capacity=loaded_capacity(i.view.definition->ammunition);
				const auto credited=ammunition::granted_reserve(native_ammo(s),{observed.ammo.loaded,observed.ammo.reserve},capacity,capacity,1000000);
				if (!credited || s.revision==UINT64_MAX)
				{ i.view.fault=true; ++failures; reason="unexplained native tube count mutation"; return false; }
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
				const auto* p=native_tube_profile(observed.native_name.data(),observed.base_capacity,scene.definition);
				if (!p || (manual(p->ammunition) && !observed.bolt_action)) return;
				if (scene.definition!=p || !scene.contact.valid || !scene.assembly || !scene.gameplay ||
					scene.owner.id()!=observed.ammo.id() || !valid_hand(scene.owner.holding_hand()) ||
					now<scene.input.sampled_at || now-scene.input.sampled_at>150ms || (!carry::active() && observed.ammo.weapon_state!=0))
				{ reason="waiting for authored tracked tube scene and native idle"; return; }
				if (vr::h2::sp::clip_capacity(ps,observed.ammo.weapon,false)!=p->ammunition.capacity)
				{ reason="modified native capacity rejected"; return; }
				const auto imported=import_native(p->ammunition,observed.ammo.weapon,++generation,
					{observed.ammo.loaded,observed.ammo.reserve});
				if (!valid(p->ammunition,imported)) return;
				for (auto& slot : inventory) if (!slot.view.active) { i=&slot; break; }
				if (!i) { reason="tube inventory full"; return; }
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
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::tube,scene.owner.id()),
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::tube,scene.owner.id(),hand_interaction::button::trigger,hand_interaction::role::supply),
				hand_interaction::input(hand(1-int(scene.owner.holding_hand())),hand_interaction::button::trigger).press,
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::tube,scene.owner.id(),hand_interaction::button::trigger,hand_interaction::role::part),hand_interaction::input(hand(1-int(scene.owner.holding_hand())),hand_interaction::button::trigger).release});
			i->view.lever=i->gesture.lever_pose();i->view.fire_armed=i->gesture.fire_armed(); i->view.rack_held=i->gesture.rack_held();i->view.travel=i->gesture.travel();i->view.rack_grip=i->gesture.rack_grip();
			i->view.sampled_at=scene.input.sampled_at; i->view.reference=scene.input.reference_generation;
			i->view.input_sequence=scene.input.sequence; reason=i->gesture.decision();
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
					for (auto& i : inventory){i.gesture={};i.gesture.restore_travel(i.view.travel);i.view.resume_transfer();}
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
					frame.manipulation=hand_interaction::permits(hand(1-int(owner.holding_hand())),hand_interaction::domain::tube,owner.id());
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
			const auto* value=find(e.id);auto view=value?value->view:presentation{};
			if(!view.active)
			{
				const auto* ps=game::g_entities[0].client;const auto observed=native_ammunition::observe_owned(ps,e.id);
				const auto* p=observed.valid?native_tube_profile(observed.native_name.data(),observed.base_capacity,s.definition):nullptr;
				if(!p || observed.ammo.id()!=e.id || (manual(p->ammunition) && !observed.bolt_action) ||
					vr::h2::sp::clip_capacity(ps,e.id.weapon,false)!=p->ammunition.capacity)continue;
				const auto preview=import_native(p->ammunition,e.id.weapon,generation+1,{observed.ammo.loaded,observed.ammo.reserve});
				if(!valid(p->ammunition,preview))continue;
				view.active=true;view.owner=live->owner;view.definition=p;view.ammo=preview;
			}
			if(!hi::sample(s,view,input))continue;
			const bool release=pump_release_held(input.input,live->owner,view.rack_held);
			const float part=rack_acquirable(s.definition->interaction,s.definition->ammunition,view.ammo,s.contact,view.travel,release)?
				s.contact.rack_distance/s.definition->interaction.rack.slide_radius:INFINITY;
			const bool supply=part>1 && s.contact.waist_distance<=s.definition->interaction.rack.waist_radius;
			const float distance=supply?s.contact.waist_distance/s.definition->interaction.rack.waist_radius:part;
			if(!std::isfinite(distance) || distance>1)continue;
			hi::offer({actor,{hi::object(hi::domain::tube,e.id,0,s.assembly),supply?hi::role::supply:hi::role::part,hi::button::trigger,hi::recipe::single,{}},edge.event,supply?30u:20u,distance,1,true,true});
		}
	}
	void update_interactions(){tick(true);}
	void update_lifecycle(bool suspended){tick(false,suspended);}
	void report_interactions()noexcept
	{
		namespace hi=hand_interaction;const std::lock_guard lock(mutex);
		for(const auto& i:inventory){const auto& v=i.view;const auto actor=interaction_hand(v);if(valid_hand(actor) && carry::contains(v.owner.id()))
			hi::observed(actor,{hi::object(hi::domain::tube,v.owner.id(),0,i.assembly),v.ammo.loader_hand!=hand::none?hi::role::supply:hi::role::part,hi::button::trigger,hi::recipe::single,{}});}
	}
	presentation current(weapon_identity id) noexcept
	{ const std::lock_guard lock(mutex); const auto* i=find(id); return i ? i->view : presentation{}; }
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
			return true; // Native drop remains possible; never persist a guessed tube.
		}
		const auto current_ammo=native_ammunition::observe_carried(game::g_entities[0].client,id);
        if(!current_ammo.valid || current_ammo.loaded!=native_ammo(i->view.ammo).loaded)return false;
        if(i->view.ammo.reserve!=current_ammo.reserve)
        {i->view.ammo.reserve=current_ammo.reserve;++i->view.ammo.revision;}
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
		return runtime_lifecycle::restore_transfer(inventory,saved,compatible,[&](auto& value){value.view.ammo.reserve=observed.ammo.reserve;++value.view.ammo.revision;value.gesture.restore_travel(saved.travel);});
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
			(active() && native_tube_profile(observed.native_name.data(),observed.base_capacity)));
	}
	bool allow_fire(const void* ps, int command, int side) noexcept
	{
		const int role=native_ammunition::local_role(ps);
		if (side || role<0) return true;
		const auto native=native_ammunition::observe_reload(ps);
		const auto& observed=native.ammo;
		const std::lock_guard lock(mutex);
		auto* i=observed.valid ? find(observed.id()) : nullptr;
		if (!i) return !active() || !native.valid || !native_tube_profile(native.native_name.data(),native.base_capacity);
		if(manual(i->view.definition->ammunition))return false; // Manual cycle never enters native rechamber scheduling.
		const auto& s=i->view.ammo; const auto now=clock::now();
		const bool allowed=i->view.owner.can_fire() && !i->view.fault && !i->view.rack_held && i->view.fire_armed && now>=i->view.sampled_at &&
			now-i->view.sampled_at<=150ms && observed.loaded==native_ammo(s).loaded && ready(i->view.definition->ammunition,s);
		return history.allow(s.weapon,s.instance_generation,command,observed.loaded,role==0,allowed);
	}
	bool allow_owned_shot(const hold& owner,const native_ammunition::snapshot& observed) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || !observed.valid || observed.id()!=owner.id() || !owner.can_fire()) return false;
		const auto native=native_ammunition::observe_owned(game::g_entities[0].client,owner.id());
		const std::lock_guard lock(mutex);auto* i=find(owner.id());
		if (!i) return !active() || !native.valid || !native_tube_profile(native.native_name.data(),native.base_capacity);
		// The independent trigger clock owns neutral rearming. Do not inherit
		// the selected viewmodel's fire_armed latch or sampled timestamp. The lever
		// additionally requires a closed/chambered mechanical state and neutral
		// Trigger history. A cosmetic closed spin tail adds no firing delay.
		if (i->view.fault || !i->view.definition || i->view.rack_held ||
			(levered(i->view.definition->ammunition) && (!i->view.fire_armed || i->view.lever.returning || clock::now()<i->view.sampled_at || clock::now()-i->view.sampled_at>150ms)) ||
			(manual(i->view.definition->ammunition) && i->view.travel>i->view.definition->interaction.rack.close_travel) || observed.loaded!=native_ammo(i->view.ammo).loaded || !ready(i->view.definition->ammunition,i->view.ammo)) return false;
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
			enabled=dvars::register_bool("vr_tubeReload",true,game::DVAR_FLAG_SAVED,"Authored VR individual-shell tube feeds");
			command::add("vr_tube_status",[] {scheduler::once([] {
				presentation v;const char* why;
				{const std::lock_guard lock(mutex);auto* i=find(last_published_scene.owner.id());v=i ? i->view : presentation{};why=reason;}
				console::info("[VR tube] contracts=%u active=%d fault=%d stored/chamber/reserve/held=%d/%d/%d/%d phase=%d rack=%d travel=%.4f lever=%.3f spin=%.3f spinning=%d operating=%d lever_grasp=%d reason=%s\n",
					boundaries.load(),v.active,v.fault,v.ammo.stored,int(v.ammo.chamber),v.ammo.reserve,v.ammo.held_rounds,int(v.ammo.phase),v.rack_held,v.travel,v.lever.open,v.lever.spin,v.lever.spinning,v.lever.operating,v.lever.grasped,why);
			},scheduler::pipeline::server);});
			// Updated by the single hand-interaction coordinator.
		}
		void pre_destroy() override { alive=false; }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::tube::component)
