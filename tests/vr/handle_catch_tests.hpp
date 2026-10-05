#pragma once
#include "component/vr/gameplay/weapons/aug/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include <limits>

namespace handle_catch_tests
{
	template<class Fixture,class Check> void run(Check&& check)
	{
		namespace w=vr::gameplay::weapons;
		namespace m=w::mechanics;
		namespace p=w::physical_reload;
		using namespace vr::gameplay::hands;
		for (const auto* descriptor:{&w::mp5::physical,&w::mp5::arctic,&w::aug::physical,
			&w::ump::physical,&w::ump::arctic,&w::ump::digital})
		{
			const auto& definition=*descriptor;
			const auto& catch_tuning=*definition.interaction.manual_catch;
			const auto& manual=*definition.interaction.manual_magazine;
			const auto roll=[](float angle){return quat{std::sin(angle/2),0,0,std::cos(angle/2)};};
			const auto configure=[&](Fixture& f,vr::hand rear) {
				f.owner.rear=rear; f.geometry.catch_input.valid=true;
				f.geometry.catch_input.slap_points.fill({0,0,.2f}); f.step();
			};
			const auto latched=[&](Fixture& f) {
				f.geometry.slide_distance=.01f; f.trigger(true);
				const auto grip=f.control.slide_grip(); f.move_slide(grip,f.tuning->slide_stroke);
				f.geometry.catch_input.rotation=roll(catch_tuning.turn_angle); f.step(); f.trigger(false);
			};
			const auto slap_point=[](Fixture& f,float z,bool world=true) {
				f.geometry.catch_input.slap_points.fill({0,0,z});
				if (world) f.geometry.catch_input.hand_world={0,0,z};
				f.step();
			};
			check(p::valid(definition.interaction),"manual handle catch tuning valid");
			for (auto rear:{vr::hand::right,vr::hand::left})
			{
				Fixture f(&definition); configure(f,rear);
				const auto initial=m::total_rounds(f.state);
				f.button(true); f.button(false);
				check(f.state.magazine_inserted && f.commits==0,"manual catch weapons have no magazine or bolt release button");
				f.geometry.slide_distance=.01f; f.trigger(true); const auto grip=f.control.slide_grip();
				f.move_slide(grip,f.tuning->full_stroke-.002f);
				f.geometry.catch_input.rotation=roll(catch_tuning.turn_angle); f.step();
				check(f.state.action==m::action_state::closed && f.spent==0,"tilting a partial pull cannot latch or extract");
				f.move_slide(grip,f.tuning->slide_stroke);
				check(f.state.action==m::action_state::latched_open && f.spent==1 && !m::ready(*f.rules,f.state),"full pull extracts once then latches and blocks fire");
				for (int cycle=0;cycle<8;++cycle)
				{
					f.geometry.catch_input.rotation=roll(0); f.step();
					check(f.state.action==m::action_state::held_open,"gentle reverse turn unlatches into the same held stroke");
					f.geometry.catch_input.rotation=roll(catch_tuning.turn_angle); f.step(); f.step(false);
					check(f.state.action==m::action_state::latched_open && f.spent==1,"rear latch toggles and duplicate frames never extract again");
				}
				f.trigger(false); for (int i=0;i<30;++i) f.step();
				check(f.state.action==m::action_state::latched_open && !f.control.slide_held() &&
					!f.control.offhand_busy(f.state) && f.control.slide_travel()==f.tuning->slide_stroke,"latched handle persists at rear and releases the hand");
				check(f.interrupt() && f.state.action==m::action_state::latched_open,"tracking interruption does not release a mechanical catch");
				configure(f,rear); f.geometry.slide_distance=1; f.geometry.magazine.grip_distance=.01f;
				f.trigger(true); const auto start=f.geometry.hand_in_gun;
				f.geometry.hand_in_gun=add(start,scale(manual.pull_axis,manual.pull_travel)); f.step();
				check(!f.state.magazine_inserted && f.state.magazine_hand!=vr::hand::none && f.state.action==m::action_state::latched_open,"latched action permits physical removal into the free hand");
				check(f.last_effect==m::effect::magazine_take && definition.interaction_sound(f.last_effect).name,
					"physical pull emits one take event with removal sound, without spawning a dropped magazine");
				f.trigger(false); f.geometry.magazine.grip_distance=1; f.geometry.waist_distance=.01f; f.trigger(true);
				f.geometry.magazine_top_in_well={0,0,-.04f}; f.step(); f.trigger(false);
				check(f.state.magazine_inserted && !f.state.chamber_loaded && f.state.action==m::action_state::latched_open,"replacement insertion leaves the caught chamber open");
				f.geometry.slide_distance=.01f; f.trigger(true);
				f.geometry.catch_input.rotation=roll(0); f.step();
				check(f.state.action==m::action_state::held_open && f.spent==1,"regrasp and reverse tilt unlock without a second extraction");
				f.trigger(false);
				check(m::ready(*f.rules,f.state) && f.state.magazine_rounds==f.rules->magazine_capacity-1 && f.spent==1 &&
					m::total_rounds(f.state)+f.spent==initial,"release after latched reload feeds exactly once and conserves ammo");
				Fixture lift(&definition); configure(lift,rear); lift.geometry.slide_distance=0; lift.trigger(true);
				const auto lg=lift.control.slide_grip(); lift.move_slide(lg,lift.tuning->slide_stroke);
				lift.geometry.hand_in_gun=add(lift.geometry.hand_in_gun,{0,0,catch_tuning.lift_distance}); lift.step();
				check(lift.state.action==m::action_state::latched_open,"gun-relative upward lift can latch without a wrist roll");
				lift.geometry.hand_in_gun=add(lg.start,scale(lift.tuning->slide_axis,lift.tuning->slide_stroke)); lift.step();
				check(lift.state.action==m::action_state::held_open && lift.spent==1,"lowering the lift unhooks without extracting again");
			}
			for (bool diagnostic:{false,true}) for (int mode=0;mode<7;++mode)
			{
				Fixture f(&definition); configure(f,vr::hand::right); latched(f);
				f.geometry.diagnose_slap=diagnostic;
				check(f.state.action==m::action_state::latched_open,"slap fixture begins caught");
				const int spent=f.spent;
				if (mode==0) { slap_point(f,.13f); slap_point(f,.075f); slap_point(f,.015f); }
				if (mode==1) { slap_point(f,.015f); slap_point(f,-.015f); } // Spawn inside.
				if (mode==2) for (int i=0;i<50;++i) slap_point(f,.13f-i*.002f); // Too slow.
				if (mode==3) { slap_point(f,-.13f); slap_point(f,-.075f); slap_point(f,-.015f); }
				if (mode==4) { slap_point(f,.13f); slap_point(f,-.13f); } // Tracking jump.
				if (mode==5) { slap_point(f,.13f); slap_point(f,.075f,false); slap_point(f,.015f,false); }
				if (mode==6) { f.owner.support=vr::hand::left; slap_point(f,.13f); slap_point(f,.075f); slap_point(f,.015f); }
				check(mode==0 ? m::ready(*f.rules,f.state) : f.state.action==m::action_state::latched_open,
					"only a fast separated downward bare-hand impact releases the caught handle");
				check(f.spent==spent,"slap return never spends another round");
				if (diagnostic)
				{
					constexpr p::slap_reason expected[]{p::slap_reason::accepted,p::slap_reason::separate,p::slap_reason::speed,
						p::slap_reason::direction,p::slap_reason::jump,p::slap_reason::world_slow,p::slap_reason::support};
					const auto trace=f.control.slap_diagnostics();
					check(trace.latest.sequence==f.input.sequence && trace.latest.reason==expected[mode],"diagnostic reports the actual consumed slap gate");
					if (mode==0)
					{
						check(trace.accepted==1 && trace.last_contact.reason==p::slap_reason::accepted && trace.last_contact.impact.hit,
							"successful native transaction is distinct from geometric contact");
						f.step(false); check(f.control.slap_diagnostics().contacts==trace.contacts,"duplicate input cannot repeat diagnostic contacts");
						f.step(); check(f.control.slap_diagnostics().latest.reason==p::slap_reason::not_latched &&
							f.control.slap_diagnostics().last_contact.sequence==trace.last_contact.sequence,"last contact remains visible after the action closes");
					}
					if (mode==5) check(trace.last_contact.impact.hit && !trace.last_contact.world_speed_ok && trace.accepted==0,
						"diagnostic distinguishes a swept hit from the stationary-wrist rejection");
				}
				else check(!f.control.slap_diagnostics().latest.sequence,"disabled diagnostics retain no samples and preserve behavior");
			}
			Fixture failed(&definition); configure(failed,vr::hand::right); latched(failed);
			failed.geometry.diagnose_slap=true;
			for (size_t hit=0;hit<hand_contact_count;++hit) for (float angle:{0.f,1.57f,3.14f})
			{
				Fixture f(&definition); configure(f,vr::hand::right); latched(f);
				f.geometry.catch_input.rotation=roll(angle);
				for (float z:{.13f,.08f,.02f})
				{
					f.geometry.catch_input.slap_points.fill({.4f,0,z});
					f.geometry.catch_input.slap_points[hit]={0,0,z}; f.geometry.catch_input.hand_world={0,0,z}; f.step();
				}
				check(m::ready(*f.rules,f.state),"any palm, finger joint or fingertip can slap with arbitrary wrist orientation");
			}
			for (float degrees:{0.f,45.f,60.f,80.f,150.f})
			{
				Fixture f(&definition); configure(f,vr::hand::right); latched(f);
				const float angle=degrees*3.14159265f/180;
				const vec direction{std::sin(angle),0,-std::cos(angle)};
				for (float distance:{.14f,.09f,.025f})
				{
					const auto point=scale(direction,-distance); f.geometry.catch_input.slap_points.fill(point);
					f.geometry.catch_input.hand_world=point; f.step();
				}
				check(degrees<=60 ? m::ready(*f.rules,f.state) : f.state.action==m::action_state::latched_open,
					"oblique downward slaps are generous while near-horizontal and upward sweeps reject");
			}
			slap_point(failed,.13f); slap_point(failed,.075f); failed.writable=false; slap_point(failed,.015f);
			check(failed.control.slap_diagnostics().last_contact.reason==p::slap_reason::rejected &&
				failed.control.slap_diagnostics().last_contact.impact.hit,"native compare rejection is preserved separately from a geometric miss");
			failed.writable=true; failed.step(); failed.step(false);
			check(failed.state.action==m::action_state::latched_open,"rejected slap cannot retry a held contact or duplicate sample");
			slap_point(failed,.075f); slap_point(failed,.13f); slap_point(failed,.075f); slap_point(failed,.015f);
			check(m::ready(*failed.rules,failed.state),"fresh separated approach can retry after a failed native compare");
			Fixture empty(&definition); configure(empty,vr::hand::right); latched(empty);
			auto s=empty.state; s.magazine_inserted=false; s.magazine_rounds=0; empty.adopt(s);
			slap_point(empty,.13f); slap_point(empty,.075f); slap_point(empty,.015f);
			check(empty.state.action==m::action_state::closed && !empty.state.chamber_loaded,"no-magazine slap closes without inventing ammunition");
			Fixture last(&definition); configure(last,vr::hand::right);
			s=last.state; s.magazine_rounds=0; s.chamber_loaded=true; last.adopt(s);
			const auto shot=m::plan(*last.rules,last.state,{m::operation::accepted_shot,last.state.weapon,last.state.instance_generation,
				last.state.revision,last.owner.rear,last.owner.rear});
			check(shot && shot.next.action==(last.rules->last_round_lock?m::action_state::locked_open:m::action_state::closed) && !shot.next.chamber_loaded,
				"last-round follower lock follows the explicit weapon policy independently of handle latching");
		}
	}
}
