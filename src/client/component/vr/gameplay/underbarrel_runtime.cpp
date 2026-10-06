#include <std_include.hpp>
#include "hand_interaction/runtime.hpp"
#include "hand_interaction/constraints.hpp"
#include "underbarrel_runtime.hpp"
#include "carry_interaction.hpp"
#include "official_cheats.hpp"
#include "underbarrel_feedback.hpp"
#include "native_ammunition.hpp"
#include "native_scripted_control.hpp"
#include "native_carry.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_feedback.hpp"
#include "weapon_interaction.hpp"
#include "component/vr/digital_button_gate.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"
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
			presentation view{};
			native::binding module{};
			std::uint64_t sequence{}, assembly{}, rear_revision{};
			hands::vec start{}, previous{};
			float start_travel{};
			bool insertion_armed{};
			clock::time_point updated{};
			int last_fire{};
			const char* decision{"waiting for fresh input"};
			std::array<float, 4> contact{};
			std::uint64_t same_hand_chords{};
			std::uint64_t native_reductions{};
			int last_native_before{}, last_native_after{};
			float start_distance{}, previous_distance{};
			controller_input::consumer_continuity continuity;
			std::uint64_t input_continuity{};
			controller_input::digital_action acquired_grip{};
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
		bool prepare_transfer_ammunition(record& module) noexcept
		{
			const auto observed = native::observe(module.module);
			if (!observed.valid || observed.ammo.loaded != module.view.ammo.loaded)
				return false;
			if (module.view.ammo.reserve != observed.ammo.reserve)
			{
				module.view.ammo.reserve = observed.ammo.reserve;
				++module.view.ammo.revision;
			}
			return true;
		}
		void rebase_stroke(record& module, const scene& contact) noexcept
		{
			module.start = module.previous = contact.stroke_hand;
			module.start_travel = module.view.travel;
			module.start_distance = module.previous_distance = contact.hand_distance;
		}

		bool synchronize_grip_binding(record& module,
		                              const scene& contact,
		                              const hold& owner,
		                              clock::time_point now) noexcept
		{
			const auto& input = contact.input;
			const bool control_changed = module.rear_revision != owner.rear_revision;
			const bool assembly_changed = module.assembly != contact.assembly;
			const bool tracking_changed = module.view.reference != input.reference_generation;
			const bool discontinuity = module.continuity.update(input, now);
			const bool older_input = input.sequence < module.sequence;
			if (!control_changed && !assembly_changed && !tracking_changed && !discontinuity && !older_input)
				return true;

			const bool retain_grip = control_changed && !assembly_changed && !tracking_changed &&
			                         !discontinuity && !older_input &&
			                         can_retain_module_grip(module.view, owner, input, module.acquired_grip);
			if (retain_grip)
			{
				// A control-hand transition is neither a stroke nor a module Trigger edge.
				rebase_stroke(module, contact);
				module.sequence = input.sequence;
			}
			else if (!interrupt(module))
				return false;

			module.rear_revision = owner.rear_revision;
			module.view.reference = input.reference_generation;
			module.assembly = contact.assembly;
			return true;
		}

		const carry::scene* find_held_scene(weapon_identity id,
		                                    std::span<const carry::instance> owned,
		                                    std::span<const carry::scene> weapons) noexcept
		{
			for (size_t i = 0; i < owned.size() && i < weapons.size(); ++i)
			{
				if (owned[i].id == id && owned[i].at == carry::location::held && weapons[i].owner.id() == id)
					return &weapons[i];
			}
			return nullptr;
		}

		void update_carry_only_grip(record& module,
		                            const hold& owner,
		                            const carry::scene* weapon,
		                            const controller_input::frame& input,
		                            clock::time_point now) noexcept
		{
			const bool scene_current = weapon && weapon->authored && weapon->assembly == module.assembly &&
			                           weapon->sequence == input.sequence &&
			                           weapon->reference == input.reference_generation;
			const bool discontinuity = module.continuity.update(input, now);
			const bool input_current =
			    fresh(input.sampled_at, now) && !discontinuity && input.sequence >= module.sequence;
			if (!scene_current || !input_current ||
			    !can_retain_module_grip(module.view, owner, input, module.acquired_grip))
			{
				interrupt(module);
				return;
			}

			// Keep the physical module hand while the host control is absent.
			// Action projection and firing resume only after a validated regrasp.
			module.view.owner = owner;
			module.view.at = module.updated = now;
			module.view.motion = {};
			module.sequence = input.sequence;
			module.rear_revision = owner.rear_revision;
			module.input_continuity = input.continuity_generation;
			publish(module);
		}
		void tick(record& runtime, const scene& contact, clock::time_point now) noexcept
		{
			const auto held = carry::held(contact.owner.id());
			const int off = valid_hand(held.rear) ? 1 - int(held.rear) : -1;
			const auto* ps = reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
			const auto& input = contact.input;
			const auto* paused = game::Dvar_FindVar("cl_paused");
			const bool usable = off >= 0 && held.can_fire() && contact.owner.id() == held.id() &&
			                    contact.owner.rear_revision == held.rear_revision && contact.gameplay &&
			                    finite_contact(contact) && input.focused && fresh(input.sampled_at, now) &&
			                    input.grip[off].valid && input.aim[off].valid && ps &&
			                    scripted_control::allowed(ps) && !(ps->e_flags & 0x103000) && paused &&
			                    !paused->current.integer && !*game::keyCatchers;
			if (!usable)
			{
				interrupt(runtime);
				return;
			}
			if (!synchronize_grip_binding(runtime, contact, held, now))
				return;
			runtime.input_continuity = contact.input.continuity_generation;
			runtime.view.owner = held;
			runtime.view.at = now;
			runtime.updated = now;
			if (runtime.view.grip == lease::none && held.support == hand::none)
				runtime.view.owns_support = false;
			auto observed = native::observe(runtime.module);
			if (!observed.valid)
			{
				reason = "module ownership or native feed rejected";
				interrupt(runtime);
				return;
			}
			const auto sync = reconcile(runtime.view.ammo, observed.module.id, observed.ammo);
			if (!sync || (sync.native_after != observed.ammo && !native::commit(observed, sync.native_after)))
			{
				runtime.view.fault = true;
				reason = "secondary budget/identity reconciliation rejected";
				interrupt(runtime);
				return;
			}
			if (sync.change == observed_change::debit)
			{
				++runtime.native_reductions;
				runtime.last_native_before = runtime.view.ammo.loaded;
				runtime.last_native_after = sync.next.loaded;
				runtime.decision = "native budget reduced; chamber unconfirmed";
			}
			if (runtime.view.ammo.chamber && !sync.next.chamber && runtime.view.grip == lease::support)
				rebase_stroke(runtime, contact);
			runtime.view.ammo = sync.next;
			runtime.view.fault = false;
			if (input.sequence == runtime.sequence)
			{
				publish(runtime);
				return;
			}
			runtime.sequence = contact.input.sequence;
			auto pinch = contact.input.trigger[off];
			const auto& squeeze = contact.input.squeeze[off];
			if (hand_interaction::input(hand(off), hand_interaction::button::trigger).release)
				pinch.down = false;
			if (hand_interaction::input(hand(off), hand_interaction::button::grip).release)
				runtime.view.grip = lease::none;
			const bool trigger_pressed =
			               hand_interaction::input(hand(off), hand_interaction::button::trigger).press,
			           grip_pressed =
			               hand_interaction::input(hand(off), hand_interaction::button::grip).press;
			if (trigger_pressed && pinch.active && pinch.down && squeeze.active && squeeze.down)
				++runtime.same_hand_chords;
			if (trigger_pressed || grip_pressed)
			{
				runtime.contact = {
				    contact.firing_distance, contact.support_facing, contact.facing, contact.waist_distance};
				runtime.decision = "input outside secondary contact";
			}
			const bool hand_available =
			    hand_interaction::permits(hand(off), hand_interaction::domain::underbarrel, held.id());
			const bool grip_down = squeeze.active && squeeze.down;
			const auto orientation = palm_facing{contact.support_facing, contact.facing};
			const auto support_tolerance = support_limits(contact.type, contact.support_release);
			const bool firing_contact =
			    contact.firing_distance <= firing_release && grip_down &&
			    (directional_grips(contact.type) ? firing_facing(orientation, true) : contact.facing >= .5f);
			if (runtime.view.grip == lease::firing &&
			    (!squeeze.active || !squeeze.down || !firing_contact || !hand_available))
				runtime.view.grip = lease::none;
			if (runtime.view.grip == lease::support && contact.type == kind::gp25)
			{
				if (!grip_down || !hand_available ||
				    contact.action_distance > std::max(support_tolerance.retention, contact.support_radius))
					runtime.view.grip = lease::none;
			}
			if (runtime.view.grip == lease::support && contact.type != kind::gp25)
			{
				const auto shared = hand_interaction::shared_slider(contact.rest_span,
				                                                    contact.axis,
				                                                    runtime.start_distance,
				                                                    runtime.previous_distance,
				                                                    contact.hand_distance,
				                                                    runtime.start_travel,
				                                                    contact.stroke,
				                                                    support_tolerance.retention,
				                                                    support_tolerance.step);
				const bool retained =
				    contact.type == kind::m203
				        ? (runtime.view.ammo.chamber ? retained_support_span(contact.rest_span,
				                                                             contact.hand_distance,
				                                                             runtime.previous_distance,
				                                                             contact.support_release,
				                                                             support_tolerance.step)
				                                     : shared.valid)
				        : contact.action_retention_distance <= support_tolerance.retention;
				if (!grip_down || !hand_available || !retained ||
				    !support_facing(orientation, true, contact.type))
					runtime.view.grip = lease::none;
				else if (!runtime.view.ammo.chamber &&
				         (contact.type == kind::m203
				              ? shared.travel > .006f
				              : hands::dot(hands::sub(contact.stroke_hand, runtime.start), contact.axis) >
				                    .003f))
					runtime.view.grip = runtime.view.support_role =
					    lease::action; // Empty action can reopen with the retained grasp.
				else
				{
					if (runtime.view.ammo.chamber && contact.type != kind::m203)
						runtime.start = contact.stroke_hand;
					runtime.previous = contact.stroke_hand;
					runtime.previous_distance = contact.hand_distance;
					publish(runtime);
					return;
				}
			}
			if (runtime.view.grip == lease::action)
			{
				auto projected = project_stroke(runtime.start,
				                                runtime.previous,
				                                contact.stroke_hand,
				                                contact.axis,
				                                runtime.start_travel,
				                                contact.stroke,
				                                support_tolerance);
				if (contact.type == kind::m203)
				{
					const auto shared = hand_interaction::shared_slider(contact.rest_span,
					                                                    contact.axis,
					                                                    runtime.start_distance,
					                                                    runtime.previous_distance,
					                                                    contact.hand_distance,
					                                                    runtime.start_travel,
					                                                    contact.stroke,
					                                                    support_tolerance.retention,
					                                                    support_tolerance.step);
					projected = {shared.valid, shared.travel};
				}
				if (!grip_down || !hand_available || !projected.valid)
					runtime.view.grip = lease::none;
				else
				{
					runtime.previous = contact.stroke_hand;
					runtime.previous_distance = contact.hand_distance;
					const auto next = projected.travel;
					if (next >= contact.stroke * .9f && !runtime.view.ammo.open)
					{
						if (!apply(runtime, operation::open, &contact))
						{
							runtime.view.grip = lease::none;
							++rejected;
						}
						else
							runtime.view.travel = next;
					}
					else if (next <= contact.stroke * .1f && runtime.view.ammo.open)
					{
						if (!apply(runtime, operation::close, &contact))
						{
							runtime.view.grip = lease::none;
							++rejected;
						}
						else
						{
							runtime.view.travel = 0;
							runtime.view.grip = runtime.view.support_role = lease::support;
							rebase_stroke(runtime, contact);
						}
					}
					else
						runtime.view.travel = next;
					publish(runtime);
					return;
				}
			}
			if (!hand_available)
			{
				// Consume blocked edges without erasing the neutral history. A hand
				// becoming free must not lose the next legitimate Grip/Trigger press
				// merely because the previous render sample still described support.
				if (trigger_pressed || grip_pressed)
					runtime.decision = "support hand occupied";
				runtime.view.grip = lease::none;
				if (runtime.view.ammo.held)
					(void)apply(runtime, operation::cleanup);
				publish(runtime);
				return;
			}
			if (runtime.view.ammo.held)
			{
				if (!pinch.active || !pinch.down || !input.trigger[off].active || !input.trigger[off].down ||
				    input.trigger[off].generation != pinch.generation)
				{
					(void)apply(runtime, operation::discard, &contact);
					runtime.insertion_armed = false;
				}
				else if (contact.load_distance > .065f)
					runtime.insertion_armed = true;
				else if (runtime.insertion_armed && contact.load_distance <= .045f &&
				         contact.load_alignment >= .35f)
				{
					runtime.insertion_armed = false;
					(void)apply(runtime, operation::insert, &contact);
				}
				publish(runtime);
				return;
			}
			if (grip_pressed && squeeze.down && runtime.view.grip == lease::none &&
			    hand_interaction::granted(hand(off),
			                              hand_interaction::domain::underbarrel,
			                              held.id(),
			                              hand_interaction::button::grip))
			{
				const auto chosen = choose_grip(runtime.view.ammo,
				                                runtime.view.travel,
				                                contact.firing_distance,
				                                contact.action_distance,
				                                orientation,
				                                contact.support_radius);
				if (chosen == lease::action || chosen == lease::support)
				{
					runtime.view.grip = runtime.view.support_role = chosen;
					runtime.view.owns_support = true;
					rebase_stroke(runtime, contact);
				}
				else if (chosen == lease::firing && grip_down)
				{
					runtime.view.grip = runtime.view.support_role = lease::firing;
					runtime.view.owns_support = true;
				}
				if (runtime.view.grip != lease::none)
					runtime.acquired_grip = squeeze;
			}
			if (grip_pressed && runtime.view.grip != lease::none)
				runtime.decision =
				    runtime.view.grip == lease::firing ? "firing grip acquired" : "action grip acquired";
			const bool can_fire_from_grip =
			    contact.type == kind::gp25
			        ? runtime.view.grip == lease::support && grip_down && hand_available
			        : runtime.view.grip == lease::firing && squeeze.active && squeeze.down &&
			              firing_contact && runtime.view.travel <= .001f;
			const auto trigger = route(trigger_pressed && pinch.down,
			                           false,
			                           can_fire_from_grip,
			                           runtime.view.grip == lease::action,
			                           contact.waist_distance <= contact.waist_radius &&
			                               (held.support == hand::none ||
			                                (runtime.view.owns_support && runtime.view.grip == lease::none)),
			                           squeeze.active && squeeze.down,
			                           true);
			if (trigger == trigger_route::fire || trigger == trigger_route::secondary_supply)
			{
				if (trigger == trigger_route::fire)
				{
					if (!input.trigger[int(held.rear)].down && input.trigger[off].active &&
					    input.trigger[off].down && input.trigger[off].generation == pinch.generation &&
					    !apply(runtime, operation::shot, &contact, true))
						++rejected;
				}
				else if (runtime.view.grip == lease::none && grip_down && input.trigger[off].active &&
				         input.trigger[off].down && input.trigger[off].generation == pinch.generation &&
				         hand_interaction::granted(hand(off),
				                                   hand_interaction::domain::underbarrel,
				                                   held.id(),
				                                   hand_interaction::button::trigger))
				{
					runtime.insertion_armed = false;
					if (!apply(runtime, operation::draw, &contact))
						++rejected;
				}
			}
			publish(runtime);
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
	void report_interactions() noexcept
	{
		namespace hi = hand_interaction;
		for (const auto& entry : records.entries())
		{
			if (!entry.id)
				continue;
			const auto& module = entry.value.view;
			if (!module.active || !carry::contains(module.owner.id()))
				continue;
			const auto target = hi::object(hi::domain::underbarrel, entry.id, 0, entry.value.assembly);
			if (module.ammo.held)
			{
				hi::observed(module.ammo.loader,
				             {target, hi::role::supply, hi::button::trigger, hi::recipe::single, {}});
				continue;
			}

			const auto actor = module_grip_hand(module);
			if (module.grip == lease::none || !valid_hand(actor))
				continue;
			const auto role = module.grip == lease::firing ? hi::role::firing : hi::role::foregrip;
			const auto abilities = carry::held(entry.id).can_fire()
			                           ? grip_capabilities(module.ammo.id.type, module.grip)
			                           : hi::capability::aim;
			hi::observed(actor, {target, role, hi::button::grip, hi::recipe::single, abilities});
		}
	}
	presentation current(weapon_identity id)noexcept
	{const std::lock_guard lock(publication);const auto* p=views.find(id);return p?*p:presentation{};}
	void suspend()noexcept
	{if(!scheduler::is_executing(scheduler::pipeline::server))return;for(auto& e:records.entries())if(e.id)interrupt(e.value);}
	bool prepare_carry_release(const hold& owner,
	                           unsigned released_hands,
	                           const controller_input::frame& input) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server))
			return false;
		auto* module = records.find(owner.id());
		if (!module)
			return true;

		if (!prepare_transfer_ammunition(*module))
			return false;

		// Carry calls this before committing the released-hand topology. Preserve
		// the still-held module here, so the later carry-only update can retain it.
		if (retain_grip_on_control_release(module->view, owner, released_hands, input, module->acquired_grip))
			return true;
		return interrupt(*module);
	}
	bool prepare_transfer(weapon_identity id) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server))
			return false;
		auto* module = records.find(id);
		return !module || (prepare_transfer_ammunition(*module) && interrupt(*module));
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
	static void update_modules(const controller_input::frame& input,
	                           std::span<const carry::instance> owned,
	                           std::span<const carry::scene> weapons,
	                           const std::array<hands::anchor, 2>& wrists,
	                           const head_pose_bridge::spatial_frame& body) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized())
			return;
		const auto* ps = reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
		if (!ps)
			return;
		if (player != ps || ps->commandTime < command_time || timeline != native_ammunition::timeline())
		{
			records = {};
			const std::lock_guard lock(publication);
			views = {};
			player = ps;
			timeline = native_ammunition::timeline();
		}
		command_time = ps->commandTime;
		if (!enabled())
		{
			suspend();
			return;
		}
		std::array<scene, 15> copy{};
		size_t count{};
		{
			const std::lock_guard lock(publication);
			for (const auto& e : scenes.entries())
				if (e.id && count < copy.size())
					copy[count++] = e.value;
		}
		records.retain([](weapon_identity id) { return carry::contains(id) || native_carry::tracks(id); });
		{
			const std::lock_guard lock(publication);
			views.retain([](weapon_identity id) { return records.find(id) != nullptr; });
		}
		for (auto& entry : records.entries())
		{
			if (!entry.id)
				continue;
			const auto owner = carry::held(entry.id);
			if (!owner.can_fire())
				update_carry_only_grip(
				    entry.value, owner, find_held_scene(entry.id, owned, weapons), input, clock::now());
		}
		for (size_t i = 0; i < count; ++i)
		{
			auto s = copy[i];
			if (!s.owner.id())
				continue;
			const auto* current_scene = find_held_scene(s.owner.id(), owned, weapons);
			if (!current_scene || !binding_current(s, *current_scene, input))
				continue;
			auto* r = records.find(s.owner.id());
			const auto previous_grip = r ? r->view.grip : lease::none;
			const auto travel = r ? r->view.travel : 0.f;
			// Every module fires, loads and acquires new grasps in the visible
			// weapon frame. Only retained shotgun motion uses the pump frame.
			s = sample_contact(s,
			                   input,
			                   current_scene->owner,
			                   current_scene->gun,
			                   wrists[s.contact_hand],
			                   body.head_position,
			                   body.head_yaw_axis,
			                   body.units_per_meter,
			                   travel);
			if (uses_pump_frame(s.type, previous_grip))
				sample_pump_motion(s, *current_scene, wrists, travel);
			// The caller already admitted current gameplay/tracking. A paused or
			// culled render sample owns local geometry, not today's input authority.
			s.gameplay = true;
			if (!r)
			{
				const auto binding = native::resolve(s.owner.id());
				if (!binding || binding.id.type != s.type)
				{
					reason = "unsupported, duplicate or aliased secondary binding";
					continue;
				}
				const auto obs = native::observe(binding);
				if (!obs.valid)
					continue;
				r = records.acquire(s.owner.id());
				if (!r)
					continue;
				r->module = binding;
				r->view.ammo = import_native(binding.id, obs.ammo.loaded, obs.ammo.reserve);
				r->view.active = valid(r->view.ammo);
				r->view.owner = s.owner;
			}
			if (r->view.active)
			{
				auto& motion = r->view.motion;
				motion.sequence = input.sequence;
				motion.assembly = s.assembly;
				motion.sampled_at = input.sampled_at;
				motion.grip_generation = input.squeeze[s.contact_hand].generation;
				motion.units = body.units_per_meter;
				motion.span = relative_hand_span(wrists, int(s.owner.rear), body.units_per_meter);
				motion.hand = s.stroke_hand;
				motion.distance = s.hand_distance;
				motion.axis = s.axis;
				motion.rest = s.rest_span;
				motion.stroke = s.stroke;
				motion.tolerance = support_limits(s.type, s.support_release);
				tick(*r, s, clock::now());
				if (!uses_pump_frame(s.type, previous_grip) && uses_pump_frame(s.type, r->view.grip))
				{
					// Seed a newly acquired pump from the same frame used next tick.
					// Switching from visible acquisition to stroke tracking is not a pull.
					sample_pump_motion(s, *current_scene, wrists, r->view.travel);
					rebase_stroke(*r, s);
					r->view.motion.hand = s.stroke_hand;
					publish(*r);
				}
			}
		}
	}
	void settle_interactions(const hand_interaction::frame& frame,
	                         const controller_input::frame& raw_input) noexcept
	{
		update_modules(frame.input, carry::interaction_instances(), frame.objects, frame.wrists, frame.body);
		bool changed{};
		for (const auto& item : carry::interaction_instances())
		{
			if (item.at != carry::location::held || !item.owner.can_fire())
				continue;
			const auto module = current(item.id);
			if (!module.owns_support)
				continue;
			const auto offhand = hand(1 - int(item.owner.rear));
			const bool grip_down = raw_input.squeeze[int(offhand)].down;
			const bool hand_occupied =
			    module.grip != lease::none && grip_down && carry::interaction_hand_occupied(offhand);
			const auto change = carry_support_handoff(
			    module, item.owner, {.grip_down = grip_down, .hand_occupied = hand_occupied});
			if (change == support_handoff::acquire)
				changed = carry::acquire_support(item.id, offhand) || changed;
			else if (change == support_handoff::release)
				changed = carry::release_support(item.id) || changed;
		}
		if (changed)
			carry::publish_topology();
	}
	part_presentation::result present(const part_rig& parts,
	                                  const hands::rig& rig,
	                                  const hands::pose_library& library,
	                                  const profile& profile,
	                                  const std::array<hands::anchor, 2>& ordinary_supports,
	                                  const controller_input::frame& input,
	                                  const hold& owner,
	                                  const presentation& v,
	                                  std::uint64_t assembly,
	                                  bool gameplay,
	                                  const std::array<hands::anchor, 2>& targets,
	                                  const std::array<hands::vec, 2>& shoulders,
	                                  const std::array<hands::vec, 3>& axes,
	                                  hands::vec head,
	                                  hands::vec offset,
	                                  float units,
	                                  std::span<hands::bone> solved,
	                                  hands::part_hand_frame* hand_motion) noexcept
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
