#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "hand_interaction/runtime.hpp"
#include "hand_interaction/mechanical_contacts.hpp"
#include "native_scripted_control.hpp"
#include "cylinder_runtime.hpp"
#include "quick_reload_runtime.hpp"
#include "reload_item_runtime.hpp"
#include "reload_item_compatibility.hpp"
#include "cylinder_presenter.hpp"
#include "native_shot_history.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapon_runtime_lifecycle.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_interaction.hpp"
#include "weapon_feedback.hpp"
#include "cylinder_profiles.hpp"
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

namespace vr::gameplay::weapons::cylinder
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
		const char* reason{"waiting for cylinder presentation contract"};
		instance* find(weapon_identity id) noexcept
		{ for (auto& i : inventory) if (i.view.active && i.view.owner.id() == id) return &i; return nullptr; }
		bool active() noexcept { return alive.load() && boundaries.load()==7 && enabled && enabled->current.enabled; }
		bool commit(instance& i, const transaction& tx, bool direct_load=false)
		{
			reload_items::key released;
			if(tx.item_released)
			{
				const auto& s=i.last_scene;
				released=reload_items::reserve(i.view.owner.id(),i.view.definition,tx.rounds_to_item,s.loader_world,s.units,
					s.input.reference_generation,clock::now());if(!released)return false;
			}
			if (!native_ammunition::compare_commit_owned(server_ps,i.view.owner.id(),tx.before,tx.after))
			{if(released)reload_items::complete_release(released,false); ++failures; reason="native compare rejected; state and escrow retained"; return false; }
			++commits;
			if(released)reload_items::complete_release(released,true);
			if (tx.feedback==effect::none || tx.silent) return true;
			auto& v=i.view;
			const auto& scene=i.last_scene;
			const auto now=clock::now();
			const bool at_loader=!direct_load && (tx.feedback==effect::draw || tx.feedback==effect::discard || tx.feedback==effect::fill);
			const auto world=at_loader ? scene.loader_world : scene.cylinder_world;
			++v.event_sequence;
			v.events[v.event_sequence%v.events.size()]={v.event_sequence,tx.feedback,now,world,scene.units,
				tx.feedback==effect::clear ? tx.cleared_live : v.ammo.held_rounds,tx.cleared_spent,tx.item_released};
			if (scene.owner.id()==v.owner.id() && scene.gameplay && now>=scene.input.sampled_at && now-scene.input.sampled_at<=100ms)
			{
				const auto kind=tx.feedback==effect::open ? mechanics::effect::action_rear :
					tx.feedback==effect::draw ? mechanics::effect::magazine_draw :
					tx.feedback==effect::fill ? mechanics::effect::magazine_in :
					tx.feedback==effect::close ? mechanics::effect::action_close : mechanics::effect::none;
				feedback::publish({kind,v.owner,scene.input.reference_generation,now,world.position,nullptr,
					v.ammo.instance_generation,v.definition,v.definition->sound_key(tx.feedback)});
			}
			return true;
		}
		bool interrupt(instance& i)
		{
			i.view.quick_load.reset();
			i.view.fire_armed=false;
			return i.gesture.interrupt(i.view.definition->ammunition,i.view.ammo,i.view.owner.holding_hand(),
				[&](const auto& tx) { return commit(i,tx); });
		}
		bool reconcile(instance& i, const native_ammunition::reload_snapshot& observed)
		{
			auto& s=i.view.ammo;
			if (i.view.fault) return false;
			if (!i.view.definition->matches_native(observed.native_name.data(),observed.base_capacity))
			{ i.view.fault=true; reason="owned cylinder definition changed"; return false; }
			if (observed.ammo.loaded!=s.live)
			{
				// A native box/pickup may credit the clip. Preserve physical contents
				// and opening; move its positive budget into reserve, not a fake load.
				const auto capacity=i.view.definition->ammunition.capacity;
				const auto credited=ammunition::granted_reserve(native_ammo(s),{observed.ammo.loaded,observed.ammo.reserve},capacity,capacity,1000000);
				if (!credited || s.revision==UINT64_MAX)
				{ i.view.fault=true; ++failures; reason="unexplained native cylinder count mutation"; return false; }
				if (!native_ammunition::commit_owned(observed.ammo,s.live,*credited))
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
				const auto* p=native_cylinder_profile(observed.native_name.data(),observed.base_capacity,scene.definition);
				if (!p) return;
				if (scene.definition!=p || !scene.contact.valid || !scene.assembly || !scene.gameplay ||
					scene.owner.id()!=observed.ammo.id() || !valid_hand(scene.owner.holding_hand()) ||
					now<scene.input.sampled_at || now-scene.input.sampled_at>150ms || (!carry::active() && observed.ammo.weapon_state!=0))
				{ reason="waiting for authored tracked cylinder scene and native idle"; return; }
				if (vr::h2::sp::clip_capacity(ps,observed.ammo.weapon,false)!=p->ammunition.capacity)
				{ reason="modified native capacity rejected"; return; }
				const auto imported=import_native(p->ammunition,observed.ammo.weapon,++generation,
					{observed.ammo.loaded,observed.ammo.reserve});
				if (!valid(p->ammunition,imported)) return;
				for (auto& slot : inventory) if (!slot.view.active) { i=&slot; break; }
				if (!i) { reason="cylinder inventory full"; return; }
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
			auto rules=i->view.definition->ammunition;rules.discard_penalty=reload_items::discard_penalty();
			i->gesture.update(i->view.definition->interaction,rules,i->view.ammo,
				scene.input,i->view.owner,g,scene.gameplay && scene.owner.id()==i->view.owner.id(),now,
				[&](const auto& tx) { return commit(*i,tx); }, {scene.manipulation,
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::cylinder,scene.owner.id()),
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::cylinder,scene.owner.id(),hand_interaction::button::trigger,hand_interaction::role::supply),
				hand_interaction::input(hand(1-int(scene.owner.holding_hand())),hand_interaction::button::trigger).press,
				hand_interaction::granted(hand(1-int(scene.owner.holding_hand())),hand_interaction::domain::cylinder,scene.owner.id(),hand_interaction::button::trigger,hand_interaction::role::part),hand_interaction::input(hand(1-int(scene.owner.holding_hand())),hand_interaction::button::trigger).release,
				true,false,std::nullopt,reload_items::can_release(i->view.definition)});
			i->view.fire_armed=i->gesture.fire_armed(); i->view.action_at=i->gesture.action_at();
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
					for (auto& i : inventory){i.gesture={};i.view.quick_load.reset();i.view.fire_armed=false;}
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
					frame.manipulation=hand_interaction::permits(hand(1-int(owner.holding_hand())),hand_interaction::domain::cylinder,owner.id());
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
			float part=INFINITY;
			const bool supply=part>1 && s.contact.waist_distance<=s.definition->interaction.waist_radius;
			const float distance=supply?s.contact.waist_distance/s.definition->interaction.waist_radius:part;
			if(!std::isfinite(distance) || distance>1)continue;
			hi::offer({actor,{hi::object(hi::domain::cylinder,e.id,0,s.assembly),supply?hi::role::supply:hi::role::part,hi::button::trigger,hi::recipe::single,{}},edge.event,supply?30u:20u,distance,1,true,true});
		}
	}
	void update_interactions(){tick(true);}
	bool fill_recovered(weapon_identity id,hand actor,int rounds,const cylinder_profile& source,const hands::anchor& world)noexcept
	{
		namespace hi=hand_interaction;using namespace hands::pose_math;
		const auto* input=hi::simulation();const auto now=clock::now();
		if(!active() || !input || !valid_hand(actor) || !hi::has(actor,hi::domain::reload_item) || !finite(world.position) ||
			!finite_part_quat(world.rotation) || !input->input.focused || now<input->input.sampled_at || now-input->input.sampled_at>150ms)return false;
		const auto* held=input->find(id);if(!held || !held->owner.can_fire() || held->owner.rear==actor || held->owner.support==actor)return false;
		const std::lock_guard lock(mutex);auto* value=find(id);const auto* cached=scenes.find(id);
		if(!value || !cached || value->view.fault || !value->view.definition || !reload_items::compatible(source,*value->view.definition))return false;
		const auto observed=native_ammunition::observe_owned(server_ps,id);if(!observed.valid || !reconcile(*value,observed))return false;
		auto scene=*cached;if(!hi::sample(scene,value->view,*input) || !input->input.trigger[int(actor)].down)return false;
		const auto& p=*value->view.definition;
		const auto face=compose(scene.cylinder_world,p.face_in_cylinder),tip=compose(world,{p.loader_tip,{0,0,0,1}});
		scene.contact.loader_in_face=hands::scale(compose(inverse(face),tip).position,1/scene.units);
		scene.contact.alignment=hands::dot(hands::rotate(world.rotation,{1,0,0}),hands::rotate(scene.cylinder_world.rotation,{1,0,0}));
		if(!value->gesture.opening_up() || !contact(p.interaction,scene.contact))return false;
		scene.loader_world=world;value->last_scene=scene;value->view.owner=scene.owner;
		auto request=cylinder::request{operation::fill_external,value->view.ammo.weapon,value->view.ammo.instance_generation,
			value->view.ammo.revision,scene.owner.rear,actor};request.external_rounds=rounds;
		const auto tx=plan(p.ammunition,value->view.ammo,request);if(!tx || !commit(*value,tx))return false;
		value->view.ammo=tx.next;return true;
	}
	void update_lifecycle(bool suspended){tick(false,suspended);}
	void report_interactions()noexcept
	{
		namespace hi=hand_interaction;const std::lock_guard lock(mutex);
		for(const auto& i:inventory){const auto& v=i.view;if(v.active && carry::contains(v.owner.id()) && valid_hand(v.owner.holding_hand()) && (v.ammo.loader_hand!=hand::none))
			hi::observed(hand(1-int(v.owner.holding_hand())),{hi::object(hi::domain::cylinder,v.owner.id(),0,i.assembly),v.ammo.loader_hand!=hand::none?hi::role::supply:hi::role::part,hi::button::trigger,hi::recipe::single,{}});}
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
			return true; // Native drop remains possible; never persist a guessed cylinder.
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
			observed.ammo.loaded==saved.ammo.live;
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
			(active() && native_cylinder_profile(observed.native_name.data(),observed.base_capacity)));
	}
	bool allow_fire(const void* ps, int command, int side) noexcept
	{
		const int role=native_ammunition::local_role(ps);
		if (side || role<0) return true;
		const auto native=native_ammunition::observe_reload(ps);
		const auto& observed=native.ammo;
		const std::lock_guard lock(mutex);
		auto* i=observed.valid ? find(observed.id()) : nullptr;
		if (!i) return !active() || !native.valid || !native_cylinder_profile(native.native_name.data(),native.base_capacity);
		const auto& s=i->view.ammo; const auto now=clock::now();
		const bool allowed=i->view.owner.can_fire() && !i->view.fault && i->view.fire_armed && now>=i->view.sampled_at &&
			now-i->view.sampled_at<=150ms && observed.loaded==s.live && ready(i->view.definition->ammunition,s);
		return history.allow(s.weapon,s.instance_generation,command,observed.loaded,role==0,allowed);
	}
	bool allow_owned_shot(const hold& owner,const native_ammunition::snapshot& observed) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || !observed.valid || observed.id()!=owner.id() || !owner.can_fire()) return false;
		const auto native=native_ammunition::observe_owned(game::g_entities[0].client,owner.id());
		const std::lock_guard lock(mutex);auto* i=find(owner.id());
		if (!i) return !active() || !native.valid || !native_cylinder_profile(native.native_name.data(),native.base_capacity);
		// The independent trigger clock owns neutral rearming. Do not inherit
		// the selected viewmodel's fire_armed latch or sampled timestamp.
		if (i->view.fault || !i->view.definition || observed.loaded!=i->view.ammo.live || !ready(i->view.definition->ammunition,i->view.ammo)) return false;
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
			enabled=dvars::register_bool("vr_cylinderReload",true,game::DVAR_FLAG_SAVED,"Authored VR cylinder feeds; no chamber bonus or native automatic reload");
			command::add("vr_cylinder_status",[] {scheduler::once([] {
				presentation copy; const char* why; std::uint64_t ok,bad,fired;
				std::uint32_t weapon{};
				{ const std::lock_guard lock(mutex); weapon=last_published_scene.owner.weapon; auto* i=find(last_published_scene.owner.id()); copy=i ? i->view : presentation{};
					why=reason; ok=commits; bad=failures; fired=shots; }
				console::info("[VR cylinder] contracts=%u active=%d fault=%d phase=%d live/cases/reserve/loader=%d/%d/%d/%d loader_hand=%d armed=%d commits/failures/shots=%llu/%llu/%llu reason=%s\n",
					boundaries.load(),copy.active,copy.fault,int(copy.ammo.phase),copy.ammo.live,copy.ammo.spent,copy.ammo.reserve,
					copy.ammo.held_rounds,int(copy.ammo.loader_hand),copy.fire_armed,ok,bad,fired,why);
				console::info("[VR cylinder presentation] %s\n",presentation_status());
				const auto native=game::CL_IsCgameInitialized() ? native_ammunition::observe_owned(game::g_entities[0].client,weapon) : native_ammunition::reload_snapshot{};
				const std::string_view native_name{native.native_name.data(),static_cast<size_t>(std::find(native.native_name.begin(),native.native_name.end(),'\0')-native.native_name.begin())};
				std::ostringstream out;
				out << "weapon=" << weapon << " native=" << native_name << " native_valid=" << native.valid
					<< " native_loaded/reserve=" << native.ammo.loaded << '/' << native.ammo.reserve
					<< " capacity=" << native.base_capacity << " contracts=" << boundaries.load()
					<< " active=" << copy.active << " fault=" << copy.fault << " phase=" << int(copy.ammo.phase)
					<< " live/cases/reserve/loader=" << copy.ammo.live << '/' << copy.ammo.spent << '/' << copy.ammo.reserve << '/' << copy.ammo.held_rounds
					<< " loader_hand=" << int(copy.ammo.loader_hand) << " armed=" << copy.fire_armed
					<< " commits/failures/shots=" << ok << '/' << bad << '/' << fired << " reason=" << why
					<< "\npresentation=" << presentation_status() << '\n';
				const auto text=out.str();
				scheduler::once([text] {utils::io::write_file_atomic("minidumps/overlord-cylinder.txt",text);},scheduler::pipeline::async);
			},scheduler::pipeline::server);});
			// Updated by the single hand-interaction coordinator.
		}
		void pre_destroy() override { alive=false; }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::cylinder::component)
