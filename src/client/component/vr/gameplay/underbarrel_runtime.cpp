#include <std_include.hpp>
#include "hand_interaction/runtime.hpp"
#include "hand_interaction/constraints.hpp"
#include "underbarrel_runtime.hpp"
#include "official_cheats.hpp"
#include "underbarrel_feedback.hpp"
#include "native_ammunition.hpp"
#include "native_scripted_control.hpp"
#include "native_carry.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_feedback.hpp"
#include "weapon_interaction.hpp"
#include "component/vr/digital_button_gate.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "part_hand_constraint.hpp"
#include "physical_reload_geometry.hpp"
#include "physical_reload_runtime.hpp"
#include "body_supply_volume.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::weapons::underbarrel
{
	namespace
	{
		using namespace hands::pose_math;
		hand_interaction::capability grip_capabilities(kind type,lease grasp)noexcept
		{
			using hand_interaction::capability;
			if(grasp==lease::firing || type==kind::gp25)return capability::aim|capability::fire;
			return type==kind::m203 || grasp==lease::support?capability::aim|capability::action:capability::action;
		}
		std::atomic_bool installed{},alive{true};
		std::mutex publication;
		instance_cache<scene,15> scenes;
		instance_cache<presentation,143> views;
		struct record
		{
			presentation view{};native::binding module{};
			std::uint64_t sequence{},assembly{},rear_revision{};hands::vec start{},previous{};float start_travel{};
			bool insertion_armed{};clock::time_point updated{};int last_fire{};
			const char* decision{"waiting for fresh input"};std::array<float,4> contact{};
			std::uint64_t same_hand_chords{};
			std::uint64_t native_reductions{};int last_native_before{},last_native_after{};
			float start_distance{},previous_distance{};
			controller_input::consumer_continuity continuity;
			std::uint64_t input_continuity{};
		};
		instance_cache<record,143> records;const void* player{};std::uint64_t timeline{};int command_time{};
		std::uint64_t shots{},commits{},rejected{};const char* reason{"waiting for supported assembly"};
		bool fresh(clock::time_point t,clock::time_point now)noexcept{return now>=t && now-t<=150ms;}
		void publish(record& r)noexcept
		{
			auto& motion=r.view.motion;
			motion.start=r.start;motion.previous=r.previous;motion.initial=r.start_travel;
			motion.start_distance=r.start_distance;motion.previous_distance=r.previous_distance;
			const std::lock_guard lock(publication);if(auto* v=views.acquire(r.view.owner.id()))*v=r.view;
		}
		bool apply(record& r,operation op,const scene* s=nullptr,bool firing_contact=false,bool unlock=false)noexcept
		{
			const auto rear=r.view.owner.rear;if(!valid_hand(rear))return false;const auto off=hand(1-int(rear));
			const auto tx=plan(r.view.ammo,{op,r.module.id,r.view.ammo.revision,rear,off,firing_contact,unlock,op==operation::shot && cheats::sustain_ammo()});
			if(!tx)
			{
				r.decision=op==operation::draw && !r.view.ammo.reserve?"secondary reserve empty":"mechanical stage rejected";
				if(s && op==operation::draw && !r.view.ammo.reserve)
				{
					feedback::event event{mechanics::effect::dry_fire,r.view.owner,s->input.reference_generation,clock::now(),s->muzzle.position};
					event.secondary_definition=r.module.id.definition;feedback::publish(event);
				}
				return false;
			}
			const auto observed=native::observe(r.module);
			if(!observed.valid || observed.ammo!=tx.before){r.decision="native module comparison rejected";return false;}
			if(op==operation::shot)
			{
				const int interval=native::interval(r.module);
				if(!s || interval<0 || command_time-r.last_fire<interval)return false;
				const auto& w=s->muzzle;
				const shot_geometry geometry{hands::rotate(w.rotation,{1,0,0}),hands::rotate(w.rotation,{0,-1,0}),hands::rotate(w.rotation,{0,0,1}),w.position};
				struct settlement{record* value;const transaction* tx;};settlement context{&r,&tx};
				if(!native::fire(observed,tx.after,geometry,command_time,[](void* p)noexcept{auto& c=*static_cast<settlement*>(p);c.value->view.ammo=c.tx->next;publish(*c.value);},&context))return false;
				r.last_fire=command_time;++shots;
				feedback::event event{mechanics::effect::shot,r.view.owner,s->input.reference_generation,clock::now(),s->muzzle.position};
				event.independent_shot=true;event.secondary_definition=r.module.id.definition;event.muzzle=s->muzzle;event.last_shot=!r.view.ammo.loaded;feedback::publish(event);
			}
			else
			{
				if(!native::commit(observed,tx.after))return false;
				r.view.ammo=tx.next;
			}
			++commits;
			r.decision=op==operation::draw?"secondary round drawn":op==operation::insert?"secondary round inserted":
				op==operation::shot?"secondary shot emitted":op==operation::open?"action opened":op==operation::close?"action closed":"held round released";
			if(s && (op==operation::open || op==operation::close || op==operation::insert))
			{
				feedback::event event{op==operation::insert?mechanics::effect::magazine_in:op==operation::open?mechanics::effect::action_rear:mechanics::effect::action_close,
					r.view.owner,s->input.reference_generation,clock::now(),s->muzzle.position};event.secondary_definition=r.module.id.definition;
				event.explicit_sound=interaction_sound(r.module.id.type,op);feedback::publish(event);
			}
			if(s && op!=operation::cleanup)feedback::carry_confirmation(off,s->input);
			return true;
		}
		bool interrupt(record& r)noexcept
		{
			r.view.grip=lease::none;r.view.motion={};r.sequence=0;r.insertion_armed=false;
			const bool ok=!r.view.ammo.held || apply(r,operation::cleanup);publish(r);return ok;
		}
		void tick(record& r,const scene& s,clock::time_point now)noexcept
		{
			const auto held=carry::held(s.owner.id());
			const int off=valid_hand(held.rear)?1-int(held.rear):-1;
			const auto* ps=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
			const auto& input=s.input;
			const auto* paused=game::Dvar_FindVar("cl_paused");
			const bool usable=off>=0 && held.can_fire() && s.owner.id()==held.id() && s.owner.rear_revision==held.rear_revision &&
				s.gameplay && finite_contact(s) && s.input.focused && fresh(s.input.sampled_at,now) && input.reference_generation==s.input.reference_generation &&
				input.focused && input.grip[off].valid && input.aim[off].valid && s.input.grip[off].valid && s.input.aim[off].valid &&
				ps && scripted_control::allowed(ps) && !(ps->e_flags&0x103000) && paused && !paused->current.integer && !*game::keyCatchers;
			if(!usable){interrupt(r);return;}
			const bool discontinuity=r.continuity.update(s.input,now);
			if(r.rear_revision!=held.rear_revision || r.view.reference!=s.input.reference_generation || r.assembly!=s.assembly ||
				discontinuity || s.input.sequence<r.sequence)
			{if(!interrupt(r))return;r.rear_revision=held.rear_revision;r.view.reference=s.input.reference_generation;r.assembly=s.assembly;}
			r.input_continuity=s.input.continuity_generation;
			r.view.owner=held;r.view.at=now;r.updated=now;
			if(r.view.grip==lease::none && held.support==hand::none)r.view.owns_support=false;
			auto observed=native::observe(r.module);
			if(!observed.valid){reason="module ownership or native feed rejected";interrupt(r);return;}
			const auto sync=reconcile(r.view.ammo,observed.module.id,observed.ammo);
			if(!sync || (sync.native_after!=observed.ammo && !native::commit(observed,sync.native_after)))
			{r.view.fault=true;reason="secondary budget/identity reconciliation rejected";interrupt(r);return;}
			if(sync.change==observed_change::debit){++r.native_reductions;r.last_native_before=r.view.ammo.loaded;r.last_native_after=sync.next.loaded;r.decision="native budget reduced; chamber unconfirmed";}
			if(r.view.ammo.chamber && !sync.next.chamber && r.view.grip==lease::support)
			{r.start=r.previous=s.local_hand;r.start_distance=r.previous_distance=s.hand_distance;r.start_travel=r.view.travel;}
			r.view.ammo=sync.next;r.view.fault=false;
			if(r.view.fault || s.input.sequence==r.sequence){publish(r);return;}
			r.sequence=s.input.sequence;
			auto pinch=s.input.trigger[off];const auto& squeeze=s.input.squeeze[off];
			if(hand_interaction::input(hand(off),hand_interaction::button::trigger).release)pinch.down=false;
			if(hand_interaction::input(hand(off),hand_interaction::button::grip).release)r.view.grip=lease::none;
			const bool pressed=hand_interaction::input(hand(off),hand_interaction::button::trigger).press,grasp=hand_interaction::input(hand(off),hand_interaction::button::grip).press;
			if(pressed && pinch.active && pinch.down && squeeze.active && squeeze.down)++r.same_hand_chords;
			if(pressed || grasp){r.contact={s.firing_distance,s.support_facing,s.facing,s.waist_distance};r.decision="input outside secondary contact";}
			const bool empty=hand_interaction::permits(hand(off),hand_interaction::domain::underbarrel,held.id());
			const bool live_grip=input.squeeze[off].active && input.squeeze[off].down && input.squeeze[off].generation==squeeze.generation;
			const auto orientation=palm_facing{s.support_facing,s.facing};
			const auto support_tolerance=support_limits(s.type,s.support_release);
			const bool contact=s.firing_distance<=firing_release && live_grip &&
				(directional_grips(s.type)?firing_facing(orientation,true):s.facing>=.5f);
			if(r.view.grip==lease::firing && (!squeeze.active || !squeeze.down || !contact || !empty))r.view.grip=lease::none;
			if(r.view.grip==lease::support && s.type==kind::gp25)
			{
				if(!live_grip || !empty || s.action_distance>std::max(support_tolerance.retention,s.support_radius))r.view.grip=lease::none;
			}
			if(r.view.grip==lease::support && s.type!=kind::gp25)
			{
				const auto shared=hand_interaction::shared_slider(s.rest_span,s.axis,r.start_distance,r.previous_distance,s.hand_distance,r.start_travel,s.stroke,support_tolerance.retention,support_tolerance.step);
				const bool retained=s.type==kind::m203 ? (r.view.ammo.chamber ?
					retained_support_span(s.rest_span,s.hand_distance,r.previous_distance,s.support_release,support_tolerance.step) : shared.valid) :
					s.action_distance<=support_tolerance.retention;
				if(!live_grip || !empty || !retained || !support_facing(orientation,true,s.type))r.view.grip=lease::none;
				else if(!r.view.ammo.chamber && (s.type==kind::m203?shared.travel>.006f:hands::dot(hands::sub(s.local_hand,r.start),s.axis)>.003f))
					r.view.grip=r.view.support_role=lease::action; // Empty action can reopen with the retained grasp.
				else {if(r.view.ammo.chamber && s.type!=kind::m203)r.start=s.local_hand;r.previous=s.local_hand;r.previous_distance=s.hand_distance;publish(r);return;}
			}
			if(r.view.grip==lease::action)
			{
				auto projected=project_stroke(r.start,r.previous,s.local_hand,s.axis,r.start_travel,s.stroke,support_tolerance);
				if(s.type==kind::m203){const auto shared=hand_interaction::shared_slider(s.rest_span,s.axis,r.start_distance,r.previous_distance,s.hand_distance,r.start_travel,s.stroke,support_tolerance.retention,support_tolerance.step);projected={shared.valid,shared.travel};}
				if(!live_grip || !empty || !projected.valid)r.view.grip=lease::none;
				else
				{
					r.previous=s.local_hand;r.previous_distance=s.hand_distance;const auto next=projected.travel;
					if(next>=s.stroke*.9f && !r.view.ammo.open)
					{if(!apply(r,operation::open,&s)){r.view.grip=lease::none;++rejected;}else r.view.travel=next;}
					else if(next<=s.stroke*.1f && r.view.ammo.open)
						{if(!apply(r,operation::close,&s)){r.view.grip=lease::none;++rejected;}else{
							r.view.travel=0;r.view.grip=r.view.support_role=lease::support;r.start=r.previous=s.local_hand;r.start_travel=0;r.start_distance=r.previous_distance=s.hand_distance;}}
					else r.view.travel=next;
					publish(r);return;
				}
			}
			if(!empty)
			{
				// Consume blocked edges without erasing the neutral history. A hand
				// becoming free must not lose the next legitimate Grip/Trigger press
				// merely because the previous render sample still described support.
				if(pressed || grasp)r.decision="support hand occupied";
				r.view.grip=lease::none;if(r.view.ammo.held)(void)apply(r,operation::cleanup);publish(r);return;
			}
			if(r.view.ammo.held)
			{
				if(!pinch.active || !pinch.down || !input.trigger[off].active || !input.trigger[off].down || input.trigger[off].generation!=pinch.generation)
				{(void)apply(r,operation::discard,&s);r.insertion_armed=false;}
				else if(s.load_distance>.065f)r.insertion_armed=true;
				else if(r.insertion_armed && s.load_distance<=.045f && s.load_alignment>=.35f)
				{r.insertion_armed=false;(void)apply(r,operation::insert,&s);}
				publish(r);return;
			}
			if(grasp && squeeze.down && r.view.grip==lease::none && hand_interaction::granted(hand(off),hand_interaction::domain::underbarrel,held.id(),hand_interaction::button::grip))
			{
				const auto chosen=choose_grip(r.view.ammo,r.view.travel,s.firing_distance,s.action_distance,orientation,s.support_radius);
				if(chosen==lease::action || chosen==lease::support)
				{r.view.grip=r.view.support_role=chosen;r.view.owns_support=true;r.start=r.previous=s.local_hand;r.start_travel=r.view.travel;r.start_distance=r.previous_distance=s.hand_distance;}
				else if(chosen==lease::firing && live_grip)
				{r.view.grip=r.view.support_role=lease::firing;r.view.owns_support=true;}
			}
			if(grasp && r.view.grip!=lease::none)r.decision=r.view.grip==lease::firing?"firing grip acquired":"action grip acquired";
			const bool fire_grip=s.type==kind::gp25?r.view.grip==lease::support && live_grip && empty:
				r.view.grip==lease::firing && squeeze.active && squeeze.down && contact && r.view.travel<=.001f;
			const auto trigger=route(pressed && pinch.down,false,fire_grip,r.view.grip==lease::action,
				s.waist_distance<=s.waist_radius && (held.support==hand::none || (r.view.owns_support && r.view.grip==lease::none)),squeeze.active && squeeze.down,true);
			if(trigger==trigger_route::fire || trigger==trigger_route::secondary_supply)
			{
				if(trigger==trigger_route::fire)
				{
					if(!input.trigger[int(held.rear)].down && input.trigger[off].active && input.trigger[off].down &&
						input.trigger[off].generation==pinch.generation && !apply(r,operation::shot,&s,true))++rejected;
				}
				else if(r.view.grip==lease::none && live_grip && input.trigger[off].active && input.trigger[off].down && input.trigger[off].generation==pinch.generation && hand_interaction::granted(hand(off),hand_interaction::domain::underbarrel,held.id(),hand_interaction::button::trigger))
				{r.insertion_armed=false;if(!apply(r,operation::draw,&s))++rejected;}
			}
			publish(r);
		}
	}
	bool enabled()noexcept{return alive && installed && carry::active() && firing_enabled();}
	void exchange_supply(const hand_interaction::frame& input)noexcept
	{
		namespace hi=hand_interaction;const auto now=clock::now();
		if(!scheduler::is_executing(scheduler::pipeline::server) || !enabled() || !input.input.focused || !fresh(input.input.sampled_at,now) ||
			input.input.orientation_settling || player!=game::g_entities[0].client || timeline!=native_ammunition::timeline())return;
		const auto* ps=reinterpret_cast<const game::playerState_s*>(player);if(!ps || ps->commandTime<command_time)return;
		bool modified{};for(auto actor:{hand::left,hand::right})
		{const auto grip=hi::input(actor,hi::button::grip);modified|=(grip.press || grip.release) && hi::input(actor,hi::button::trigger).down;}
		if(!modified)return;
		std::array<scene,15> copy{};size_t count{};
		{const std::lock_guard lock(publication);for(const auto& e:scenes.entries())if(e.id && count<copy.size())copy[count++]=e.value;}
		for(size_t n=0;n<count;++n)
		{
			auto s=copy[n];const auto* live=input.find(s.owner.id());
			if(!live || !binding_current(s,*live,input.input) || !live->owner.can_fire() || live->owner.support!=hand::none)continue;
			const auto actor=hand(s.contact_hand);if(!(input.valid_hands&(1u<<s.contact_hand)))continue;
			const auto from=hi::pose(actor).driver;if(from.object!=s.owner.id())continue;
			const auto selected=hi::supply_selection(from.provider,hi::input(actor,hi::button::trigger),hi::input(actor,hi::button::grip),
				input.waist(actor,s.supply,s.waist_radius)<=s.waist_radius);
			if(selected==hi::domain::none)continue;
			auto* r=records.find(s.owner.id());
			if(!r || !r->view.active || r->view.fault || r->view.grip!=lease::none || r->assembly!=s.assembly ||
				r->rear_revision!=live->owner.rear_revision || r->view.reference!=input.input.reference_generation ||
				r->input_continuity!=input.input.continuity_generation ||
				(!input.input.continuity_generation && !fresh(r->updated,now)) || input.input.sequence<=r->sequence)continue;
			const bool primary=selected==hi::domain::magazine;
			if(primary ? r->view.ammo.loader!=actor : r->view.ammo.held!=0)continue;
			const auto observed=native::observe(r->module);if(!observed.valid)continue;
			const auto sync=reconcile(r->view.ammo,observed.module.id,observed.ammo);
			if(!sync || sync.native_after!=observed.ammo)continue;
			const auto tx=plan(sync.next,{primary?operation::cleanup:operation::draw,r->module.id,sync.next.revision,live->owner.rear,actor});
			if(!tx)continue;
			struct exchange {weapon_identity id;hand actor;bool primary;native::observation observed;int reserve;};
			exchange context{s.owner.id(),actor,primary,observed,tx.after.reserve};
			const auto write=[](void* data)noexcept{
				auto& c=*static_cast<exchange*>(data);
				return physical_reload::exchange_supply(c.id,c.actor,c.primary,
					[](const native_ammunition::snapshot& host,int reserve,void* payload)noexcept{
						const auto& change=*static_cast<exchange*>(payload);return native::exchange_reserves(change.observed,host,reserve,change.reserve);
					},data);
			};
			const hi::grasp to{hi::object(selected,s.owner.id(),0,primary?live->assembly:s.assembly),hi::role::supply,hi::button::trigger,hi::recipe::single,{}};
			if(!hi::exchange_supply(actor,from,to,write,&context)){++rejected;r->decision="supply exchange rejected; original payload retained";continue;}
			r->view.ammo=tx.next;r->view.owner=live->owner;r->view.at=r->updated=now;r->sequence=input.input.sequence;r->insertion_armed=false;
			r->decision=primary?"secondary returned; primary magazine selected":"primary magazine returned; secondary selected";
			++commits;publish(*r);feedback::carry_confirmation(actor,input.input);
		}
	}
	void collect_interactions(const hand_interaction::frame& input)noexcept
	{
		namespace hi=hand_interaction;if(!enabled())return;
		std::array<scene,15> copy{};size_t count{};{const std::lock_guard lock(publication);for(const auto& e:scenes.entries())if(e.id && count<copy.size())copy[count++]=e.value;}
		for(size_t i=0;i<count;++i)
		{
			auto s=copy[i];const auto* live=input.find(s.owner.id());if(!live || !binding_current(s,*live,input.input))continue;
			const auto actor=hand(s.contact_hand);const auto grip=hi::input(actor,hi::button::grip),pinch=hi::input(actor,hi::button::trigger);
			if(grip.down && hi::free(actor))hi::select_supply(actor,hi::domain::underbarrel,s.owner.id());
			if(!grip.press && !pinch.press)continue;
			const auto view=current(s.owner.id());auto ammo=view.ammo;
			if(!view.active){const auto binding=native::resolve(s.owner.id());if(!binding)continue;const auto obs=native::observe(binding);if(!obs.valid)continue;ammo=import_native(binding.id,obs.ammo.loaded,obs.ammo.reserve);}
			s=sample_contact(s,input.input,live->owner,live->gun,input.wrists[s.contact_hand],input.body.head_position,input.body.head_yaw_axis,input.body.units_per_meter,view.travel);
			if(grip.press && grip.down)
			{
				const auto picked=choose_grip(ammo,view.travel,s.firing_distance,s.action_distance,{s.support_facing,s.facing},s.support_radius);
				if(picked!=lease::none)hi::offer({actor,{hi::object(hi::domain::underbarrel,s.owner.id(),0,s.assembly),picked==lease::firing?hi::role::firing:hi::role::foregrip,hi::button::grip,hi::recipe::single,grip_capabilities(s.type,picked)},grip.event,15,picked==lease::firing?s.firing_distance/firing_acquire:s.action_distance/s.support_radius,1,true,true});
			}
			if(pinch.press && pinch.down && grip.down && s.waist_distance<=s.waist_radius)
				hi::offer({actor,{hi::object(hi::domain::underbarrel,s.owner.id(),0,s.assembly),hi::role::supply,hi::button::trigger,hi::recipe::single,{}},pinch.event,30,s.waist_distance/s.waist_radius,1,true,true});
		}
	}
	void report_interactions()noexcept
	{
		namespace hi=hand_interaction;
		for(const auto& e:records.entries())if(e.id){const auto& v=e.value.view;if(!v.active || !carry::contains(v.owner.id()) || !valid_hand(v.owner.rear))continue;
			if(v.ammo.held)hi::observed(v.ammo.loader,{hi::object(hi::domain::underbarrel,e.id,0,e.value.assembly),hi::role::supply,hi::button::trigger,hi::recipe::single,{}});
			else if(v.grip!=lease::none)hi::observed(hand(1-int(v.owner.rear)),{hi::object(hi::domain::underbarrel,e.id,0,e.value.assembly),v.grip==lease::firing?hi::role::firing:hi::role::foregrip,hi::button::grip,hi::recipe::single,grip_capabilities(v.ammo.id.type,v.grip)});}
	}
	presentation current(weapon_identity id)noexcept
	{const std::lock_guard lock(publication);const auto* p=views.find(id);return p?*p:presentation{};}
	void suspend()noexcept
	{if(!scheduler::is_executing(scheduler::pipeline::server))return;for(auto& e:records.entries())if(e.id)interrupt(e.value);}
	bool prepare_transfer(weapon_identity id)noexcept
	{
		if(!scheduler::is_executing(scheduler::pipeline::server))return false;auto* r=records.find(id);if(!r)return true;
		const auto observed=native::observe(r->module);if(!observed.valid || observed.ammo.loaded!=r->view.ammo.loaded)return false;
		if(r->view.ammo.reserve!=observed.ammo.reserve){r->view.ammo.reserve=observed.ammo.reserve;++r->view.ammo.revision;}
		return interrupt(*r);
	}
	bool restore_transfer(const presentation& saved)noexcept
	{
		if(!saved.active)return true;
		if(!scheduler::is_executing(scheduler::pipeline::server) || !valid(saved.ammo) || saved.ammo.held || valid_hand(saved.ammo.loader))return false;
		const auto binding=native::resolve(saved.owner.id());const auto observed=native::observe(binding);
		if(!observed.valid || binding.id!=saved.ammo.id || !native::commit(observed,{saved.ammo.loaded,saved.ammo.reserve}))return false;
		auto* r=records.acquire(saved.owner.id());if(!r)return false;*r={};r->module=binding;r->view=saved;
		r->view.grip=r->view.support_role=lease::none;r->view.owns_support=false;r->view.motion={};r->view.reference=0;r->view.at={};
		player=game::g_entities[0].client;timeline=native_ammunition::timeline();command_time=reinterpret_cast<const game::playerState_s*>(player)->commandTime;
		publish(*r);return true;
	}
	bool blocks_native(const void* ps)noexcept
	{
		if(!enabled() || native_ammunition::local_role(ps)<0)return false;
		std::uint32_t token{},flags{};std::memcpy(&token,static_cast<const std::byte*>(ps)+0x3bc,4);std::memcpy(&flags,static_cast<const std::byte*>(ps)+0x3c0,4);
		return flags&0x4000 && current(carry::native_identity(token)).active;
	}
	bool ordinary_support_allowed(const carry::scene& weapon,const hands::anchor& wrist,hand actor,bool retaining)noexcept
	{
		if(!enabled() || !valid_hand(actor))return true;
		scene s;{const std::lock_guard lock(publication);const auto* cached=scenes.find(weapon.owner.id());if(!cached)return true;s=*cached;}
		if(!directional_grips(s.type))return true;
		if(s.assembly!=weapon.assembly)return true; // Old attachment policy cannot restrict a different rig.
		if(retaining)
		{
			// Same solved two-hand anchor and authored positional hysteresis as
			// the original presenter, plus the broader retained palm gate.
			if(!weapon.authored || !std::isfinite(s.units) || s.units<=0)return false;
			const auto point=hands::pose_math::compose(weapon.gun,weapon.supports[int(actor)]).position;
			const auto q=hands::multiply(hands::conjugate(weapon.gun.rotation),wrist.rotation);
			return hands::length(hands::sub(wrist.position,point))/s.units<=weapon.authored->release_meters &&
				support_facing(controller_facing(q,int(actor)),true,s.type);
		}
		const auto module=current(weapon.owner.id());
		if(module.active && (module.ammo.open || module.travel>.001f))return false; // Moving barrel/pump owns the real shifted contact.
		const auto q=hands::multiply(hands::conjugate(weapon.gun.rotation),wrist.rotation);
		const auto local=hands::pose_math::compose(hands::pose_math::inverse(weapon.gun),wrist);
		const float distance=firing_distance(hands::scale(hands::sub(local.position,s.firing_local.position),1/s.units),s.firing_forward_m);
		return support_intent(distance,controller_facing(q,int(actor)),s.type);
	}
	void update(const controller_input::frame& input,std::span<const carry::instance> owned,std::span<const carry::scene> weapons,
		const std::array<hands::anchor,2>& wrists,const head_pose_bridge::spatial_frame& body)noexcept
	{
		if(!scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized())return;
		const auto* ps=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);if(!ps)return;
		if(player!=ps || ps->commandTime<command_time || timeline!=native_ammunition::timeline())
		{records={};const std::lock_guard lock(publication);views={};player=ps;timeline=native_ammunition::timeline();}
		command_time=ps->commandTime;
		if(!enabled()){suspend();return;}
		std::array<scene,15> copy{};size_t count{};
		{const std::lock_guard lock(publication);for(const auto& e:scenes.entries())if(e.id && count<copy.size())copy[count++]=e.value;}
		records.retain([](weapon_identity id){return carry::contains(id)||native_carry::tracks(id);});
		{const std::lock_guard lock(publication);views.retain([](weapon_identity id){return records.find(id)!=nullptr;});}
		for(auto& e:records.entries())if(e.id && !carry::held(e.id).can_fire())interrupt(e.value);
		for(size_t i=0;i<count;++i)
		{
			auto s=copy[i];if(!s.owner.id())continue;
			const carry::scene* current_scene{};
			for(size_t n=0;n<owned.size() && n<weapons.size();++n)if(owned[n].id==s.owner.id() && owned[n].at==carry::location::held){current_scene=&weapons[n];break;}
			if(!current_scene || !binding_current(s,*current_scene,input))continue;
			auto* r=records.find(s.owner.id());
			// A retained slider/pump grasp must measure motion in the same rear-
			// driven frame before and after closure, not in its own IK feedback.
			const auto gun=s.type!=kind::m203 && r && (r->view.grip==lease::action || r->view.grip==lease::support)?mechanical_frame(*current_scene,wrists):current_scene->gun;
			s=sample_contact(s,input,current_scene->owner,gun,wrists[s.contact_hand],body.head_position,body.head_yaw_axis,body.units_per_meter,r?r->view.travel:0);
			// The caller already admitted current gameplay/tracking. A paused or
			// culled render sample owns local geometry, not today's input authority.
			s.gameplay=true;
			if(!r)
			{
				const auto binding=native::resolve(s.owner.id());
				if(!binding || binding.id.type!=s.type){reason="unsupported, duplicate or aliased secondary binding";continue;}
				const auto obs=native::observe(binding);if(!obs.valid)continue;
				r=records.acquire(s.owner.id());if(!r)continue;
				r->module=binding;r->view.ammo=import_native(binding.id,obs.ammo.loaded,obs.ammo.reserve);r->view.active=valid(r->view.ammo);r->view.owner=s.owner;
			}
			if(r->view.active)
			{
				auto& motion=r->view.motion;
				motion.sequence=input.sequence;motion.assembly=s.assembly;motion.sampled_at=input.sampled_at;
				motion.grip_generation=input.squeeze[s.contact_hand].generation;motion.units=body.units_per_meter;
				motion.span=relative_hand_span(wrists,int(s.owner.rear),body.units_per_meter);
				motion.hand=s.local_hand;motion.distance=s.hand_distance;motion.axis=s.axis;motion.rest=s.rest_span;
				motion.stroke=s.stroke;motion.tolerance=support_limits(s.type,s.support_release);
				const bool moving=r->view.grip==lease::action || r->view.grip==lease::support;
				tick(*r,s,clock::now());
				if(s.type!=kind::m203 && !moving && r->view.grip==lease::action)
				{
					// Acquisition used the visible contact. Seed travel in the same
					// rear-driven frame used next tick; changing IK frames is not a pull.
					const auto stable=sample_contact(s,input,current_scene->owner,mechanical_frame(*current_scene,wrists),
						wrists[s.contact_hand],body.head_position,body.head_yaw_axis,body.units_per_meter,r->view.travel);
					r->start=r->previous=stable.local_hand;r->start_travel=r->view.travel;
					r->view.motion.hand=stable.local_hand;
					publish(*r);
				}
			}
		}
	}
	part_presentation::result present(const part_rig& parts,const hands::rig& rig,const hands::pose_library& library,const profile& profile,
		const std::array<hands::anchor,2>& ordinary_supports,
		const controller_input::frame& input,const hold& owner,const presentation& v,std::uint64_t assembly,bool gameplay,
		const std::array<hands::anchor,2>& targets,const std::array<hands::vec,2>& shoulders,const std::array<hands::vec,3>& axes,
		hands::vec head,hands::vec offset,float units,std::span<hands::bone> solved,hands::part_hand_frame* hand_motion)noexcept
	{
		using namespace hands;if(!enabled() || !parts || !owner.can_fire() || !library.valid || units<=0 || !std::isfinite(units) || solved.size()<size_t(rig.count))return {};
		const int rear=int(owner.rear),off=1-rear;const auto gun=as_anchor(solved[rig.gun]);
		auto fire=firing_anchor(parts,rig,library,ordinary_supports[off],off);
		auto rack=parts.type==kind::shotgun ? compose(parts.mount,authored::pump_grip) : ordinary_supports[off];
		auto round=parts.type==kind::m203 ? authored::m203_round_in_wrist : parts.type==kind::gp25 ? authored::gp25_round_in_wrist : authored::shotgun_round_in_wrist;
		if(off==1)
		{
			if(parts.type==kind::shotgun)rack=hands::pose_mirror::wrist(rack,library.mirror_basis[rig.arms[off].wrist]);
			round=hands::pose_mirror::object_in_wrist({},round,library.mirror_basis[rig.arms[off].wrist]);
		}
		const auto axis=rotate(parts.mount.rotation,parts.type==kind::shotgun ? authored::shotgun_axis : authored::m203_axis);
		const auto action_rest=rack;const auto movement=scale(axis,v.travel*units);rack.position=add(rack.position,movement);
		const auto load=parts.type==kind::gp25 ? parts.muzzle : parts.round_rest;
		scene s;s.input=input;s.owner=owner;s.type=parts.type;s.assembly=assembly;s.gameplay=gameplay;
		s.axis=axis;s.contact_hand=off;s.wrist_basis=free_hand_rotation(profile,off);
		s.firing_forward_m=parts.firing_forward_m;
		s.rear_local=profile.wrists[rear].position;s.support_radius=profile.acquire_meters;s.support_release=profile.release_meters;
		s.firing_local=fire;s.action_local=action_rest;s.round_in_wrist=round;s.loading_local=load;s.muzzle_local=parts.muzzle;
		s.stroke=parts.type==kind::shotgun ? authored::shotgun_stroke : authored::m203_stroke;s.units=units;
		if(profile.reload){s.waist_radius=profile.reload->interaction.waist_radius;s.supply=profile.reload->supply;}
		s=sample_contact(s,input,owner,gun,targets[off],head,axes,units,v.travel);s.muzzle.position=add(s.muzzle.position,offset);
		if(!finite_contact(s))s.gameplay=false;
		{const std::lock_guard lock(publication);scenes.retain([](weapon_identity id){return carry::contains(id);});if(auto* p=scenes.acquire(owner.id()))*p=s;}
		if(!finite_contact(s))return {};
		part_presentation::result out;out.valid=true;
		const auto plan=hand_interaction::pose(hand(off));const bool pose_owned=plan.driver.provider==hand_interaction::domain::underbarrel && plan.driver.object==owner.id();
		if(pose_owned && v.active && !v.fault && (v.grip==lease::firing || v.grip==lease::action))
		{
			const auto wrist=compose(gun,v.grip==lease::firing?fire:rack);
			if(constrain_part_hand(rig,library,profile,targets,shoulders,axes,rear,wrist,solved))out.posed_hands|=1u<<off;
			const auto fingers=v.grip==lease::firing ? parts.firing_fingers : parts.type==kind::m203?profile.fingers:std::span<const joint_pose>(authored::pump_fingers);
			hands::pose_mirror::fingers(rig,library,profile,fingers,off,solved,off==1 && !(v.grip==lease::action && parts.type==kind::m203));
		}
		if(hand_motion && pose_owned)hand_motion->apply(off,
			out.posed_hands ? part_hand_attachment::underbarrel_action : v.active && v.ammo.held ? part_hand_attachment::underbarrel_round : part_hand_attachment::free);
		(void)pose_action(parts,rig,gun,v.travel,units,solved);
		if(pose_owned && v.active && v.ammo.held && v.ammo.loader==hand(off))
		{
			const auto held_round=compose(as_anchor(solved[rig.arms[off].wrist]),round);
			move_part(rig,parts.round,held_round,solved);
			for(int n=0;n<2;++n)if(parts.round_parts[n]>=0)move_part(rig,parts.round_parts[n],compose(held_round,parts.round_parts_local[n]),solved);
			const auto fingers=parts.type==kind::shotgun?std::span<const joint_pose>(authored::shell_fingers):parts.type==kind::gp25?std::span<const joint_pose>(authored::gp25_fingers):std::span<const joint_pose>(authored::grenade_fingers);
			hands::pose_mirror::fingers(rig,library,profile,fingers,off,solved,off==1);out.posed_hands|=1u<<off;
		}
		else for(int i=0;i<rig.count;++i)if(descendant(i,parts.round,rig))out.hidden[i/32]|=0x80000000u>>(i%32);
		return out;
	}
	hands::anchor support_anchor(const part_rig& parts,const hands::rig& rig,const hands::pose_library& library,hands::anchor ordinary,
		int hand,const presentation& v,float units)noexcept
	{
		if(v.support_role==lease::firing)return firing_anchor(parts,rig,library,ordinary,hand);
		auto result=parts.type==kind::shotgun?compose(parts.mount,authored::pump_grip):ordinary;
		if(hand==1 && parts.type==kind::shotgun)result=hands::pose_mirror::wrist(result,library.mirror_basis[rig.arms[1].wrist]);
		const auto axis=hands::rotate(parts.mount.rotation,parts.type==kind::shotgun?authored::shotgun_axis:authored::m203_axis);
		result.position=hands::add(result.position,hands::scale(axis,v.travel*units));return result;
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			installed=native::initialize();
			command::add("vr_underbarrel_status",[]{scheduler::once([]{
				std::ostringstream out;out<<"ready="<<enabled()<<" shots="<<shots<<" commits="<<commits<<" rejected="<<rejected<<" reason="<<reason<<'\n';
				for(const auto& e:records.entries())if(e.id){const auto& r=e.value;const auto& v=r.view;out<<"host="<<e.id.weapon<<" generation="<<e.id.generation<<" module="<<v.ammo.id.definition<<" kind="<<int(v.ammo.id.type)
					<<" loaded="<<v.ammo.loaded<<" reserve="<<v.ammo.reserve<<" held="<<v.ammo.held<<" chamber="<<v.ammo.chamber<<" open="<<v.ammo.open<<" spent="<<v.ammo.spent<<" travel="<<v.travel<<" grip="<<int(v.grip)<<" fault="<<v.fault
					<<" input="<<r.sequence<<" same_hand_chords="<<r.same_hand_chords<<" native_reductions="<<r.native_reductions<<" last_native="<<r.last_native_before<<"->"<<r.last_native_after
					<<" decision="<<r.decision<<" fire_distance/up/inward/waist="<<r.contact[0]<<'/'<<r.contact[1]<<'/'<<r.contact[2]<<'/'<<r.contact[3]<<'\n';}
				const auto text=out.str();console::info("[VR underbarrel] %s",text.c_str());scheduler::once([text]{utils::io::write_file_atomic("minidumps/h2-mod-vr-underbarrel.txt",text);},scheduler::pipeline::async);
			},scheduler::pipeline::server);});
		}
		void pre_destroy()override{alive=false;}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::underbarrel::component)
