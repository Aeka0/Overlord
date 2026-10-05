#pragma once
#include "component/vr/gameplay/weapons/de50/profile.hpp"
#include "component/vr/gameplay/weapons/m1911/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/m93r/profile.hpp"
#include "component/vr/gameplay/weapons/usp/profile.hpp"
#include "component/vr/gameplay/knife_profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"

namespace knife_slide_tests
{
	template<class Fixture,class Check> void run(Check&& check)
	{
		using namespace vr::gameplay;
		using namespace weapons;
		using namespace hands;
		using namespace vr::gameplay::hands::pose_math;
		for (const auto* p:reload_profiles)
		{
			const bool supported=p==&usp::physical || p==&usp::silenced_physical || p==&m9::physical || p==&m1911::physical || p==&de50::physical || p==&m93r::physical;
			check(!p->knife_slide_grips.empty()==supported && p->knife_slide_grips.size()==p->interaction.knife_slide_pose_count,
				"knife slide capability and geometry agree only for the five admitted pistol families");
		}
		for (const auto* p:{&usp::physical,&usp::silenced_physical,&m9::physical,&m1911::physical,&de50::physical,&m93r::physical})
			for (hand rear:{hand::left,hand::right})
				for (auto mode:{equipment::knife_grip::forward,equipment::knife_grip::reverse})
					for (bool return_knife:{false,true})
					{
						Fixture f(p);f.owner.rear=rear;f.step();const auto off=hand(1-int(rear));
						equipment::knife_state knife;knife.take(off,mode);
						f.input.squeeze[int(off)]={true,true,1,1};f.geometry.knife_held=true;
						f.geometry.slide_distance=f.geometry.waist_distance=0;
						f.trigger(true);
						check(f.control.slide_held() && f.control.knife_slide_grasp() && f.state.magazine_hand==hand::none,
							"fresh knife pinch selects the slide before overlapping magazine supply");
						const auto grasp=f.control.slide_grip();const int total=mechanics::total_rounds(f.state);
						f.move_slide(grasp,p->interaction.full_stroke*.45f);f.move_slide(grasp,0);
						check(f.spent==0 && f.state.chamber_loaded,"partial knife slide stroke never ejects or feeds ammunition");
						f.move_slide(grasp,p->interaction.slide_stroke);const auto commits=f.commits;
						f.step(false);f.step();
						check(f.spent==1 && f.commits==commits && !f.state.chamber_loaded,
							"one full knife pull extracts exactly once despite duplicate or held rear samples");
						if(return_knife)
						{knife.release(1u<<int(off));f.input.squeeze[int(off)].down=false;f.geometry.knife_held=false;f.step();}
						physical_reload::presentation view;view.active=true;view.definition=p;
						view.slide_held=f.control.slide_held();view.knife_slide_grasp=f.control.knife_slide_grasp();
						check(view.use_knife_slide_grasp(false) && f.control.slide_grip().pose==grasp.pose,
							"returning knife while holding Trigger cannot change the latched slide pose set");
						f.geometry.slide_pose=no_part_grip;f.geometry.slide_distance=1; // Turning away cannot select another held style.
						f.move_slide(grasp,0);check(f.state.chamber_loaded && f.spent==1,"forward knife stroke chambers normally");
						f.move_slide(grasp,p->interaction.slide_stroke);f.trigger(false);
						check(!f.control.slide_held() && f.state.chamber_loaded && f.spent==2 &&
							mechanics::total_rounds(f.state)+f.spent==total,"repeated held stroke and Trigger release conserve rounds and feed once per cycle");
						check(knife.holder==(return_knife ? hand::none : off) && knife.grip==mode,
							"slide operations never release or reverse the Grip-owned knife");
						view.slide_held=false;check(!view.use_knife_slide_grasp(false),"slide release drops the retained co-grasp");
						f.geometry.knife_held=false;f.geometry.slide_distance=0;f.geometry.slide_pose=0;f.trigger(true);
						check(f.control.slide_held() && !f.control.knife_slide_grasp(),"next bare-hand acquisition restores the ordinary pistol grasp");
					}
		for (const auto* p:{&usp::physical,&m9::physical,&m1911::physical,&de50::physical,&m93r::physical})
		{
			Fixture f(p);f.geometry.knife_held=true;f.geometry.slide_distance=0;
			f.geometry.slide_pose=static_cast<std::uint8_t>(p->knife_slide_grips.size());f.trigger(true);
			check(!f.control.slide_held(),"out-of-range knife style cannot index ordinary pose data");
			f.geometry.slide_pose=0;f.step();check(!f.control.slide_held(),"invalid style rejection consumes the fresh pinch");
			f.trigger(false);f.trigger(true);f.writable=false;f.move_slide(f.control.slide_grip(),p->interaction.slide_stroke);
			check(f.spent==0 && f.state.chamber_loaded && !f.control.slide_held(),"failed native extraction cancels knife stroke without ammunition loss");
			f.writable=true;f.step();check(!f.control.slide_held(),"failed stroke cannot restart from held Trigger");
			for (int reason=0;reason<4;++reason)
			{
				Fixture lost(p);lost.geometry.knife_held=true;lost.geometry.slide_distance=0;lost.trigger(true);
				lost.move_slide(lost.control.slide_grip(),p->interaction.slide_stroke);const auto total=mechanics::total_rounds(lost.state)+lost.spent;
				if(reason==0)lost.input.focused=false;
				if(reason==1)lost.input.grip[0].valid=false;
				if(reason==2){++lost.input.reference_generation;++lost.geometry.reference_generation;}
				if(reason==3)lost.geometry.hand_in_gun[1]+=1;
				lost.step();check(!lost.control.slide_held() && lost.state.action!=mechanics::action_state::held_open &&
					mechanics::total_rounds(lost.state)+lost.spent==total,"lost focus/tracking/recenter or separated hand safely closes knife-held slide");
				lost.input.focused=true;lost.input.grip[0].valid=true;lost.step();
				check(!lost.control.slide_held(),"interrupted knife slide requires neutral Trigger before rearming");
			}
			Fixture empty(p);auto state=empty.state;state.magazine_rounds=0;state.chamber_loaded=false;state.action=mechanics::action_state::locked_open;
			empty.adopt(state);empty.geometry.knife_held=true;empty.button(true);empty.button(false);
			empty.geometry.waist_distance=0;empty.trigger(true);empty.geometry.magazine_top_in_well={0,0,0};empty.step();
			empty.geometry.slide_distance=0;empty.step();check(!empty.control.slide_held(),"insertion cannot hand off directly to slide while Trigger remains held");
			empty.trigger(false);empty.trigger(true);const auto grip=empty.control.slide_grip();
			check(empty.control.slide_held() && std::abs(grip.initial_travel-p->interaction.locked_travel)<1e-6f,"knife acquires at current follower-lock travel");
			empty.move_slide(grip,p->interaction.slide_stroke);empty.trigger(false);
			check(empty.state.chamber_loaded && empty.native.loaded==p->ammunition.magazine_capacity && empty.spent==0,
				"empty knife magazine reload can chamber by physically pulling and releasing the slide");
			// An unreviewed pistol must retain the old occupancy restriction.
			auto disabled=p->interaction;disabled.knife_slide_pose_count=0;Fixture blocked(p);blocked.tuning=&disabled;
			blocked.geometry.knife_held=true;blocked.geometry.slide_distance=0;blocked.trigger(true);
			check(!blocked.control.slide_held(),"missing knife slide capability remains closed");
		}
		for (const quat basis:{quat{0,0,0,1},normalize(quat{.31f,-.27f,.52f,.73f})})
			for (bool right:{false,true})
			{
				const auto forward=equipment::knife_profile::attachment(equipment::knife_grip::forward,basis,right,equipment::knife_hand_pose::slide);
				const auto reverse=equipment::knife_profile::attachment(equipment::knife_grip::reverse,basis,right,equipment::knife_hand_pose::slide);
				const vec native_contact{3.63002227f,.68731740f,.93354005f};
				check(length(sub(compose(reverse,{equipment::knife_profile::grip_center,{0,0,0,1}}).position,
					right ? vr::gameplay::hands::pose_mirror::local_point(native_contact,basis) : native_contact))<.0001f,
					"slide knife attachment preserves the native USP pickup frame-17 handle contact");
				check(length(sub(compose(forward,{equipment::knife_profile::grip_center,{0,0,0,1}}).position,
					compose(reverse,{equipment::knife_profile::grip_center,{0,0,0,1}}).position))<.0001f &&
					dot(rotate(forward.rotation,{1,0,0}),rotate(reverse.rotation,{1,0,0}))<-.999f,
					"slide co-grasp changes knife direction without moving its handle contact in either hand");
				for (const auto* p:{&usp::physical,&m9::physical,&m1911::physical,&de50::physical,&m93r::physical})
				{
					const auto source=p->knife_slide_grips[0];const auto pose=right ? vr::gameplay::hands::pose_mirror::part(source,basis) : source;
					for (float travel:{0.f,p->interaction.locked_travel,p->interaction.slide_stroke})
					{
						const auto offset=scale(p->interaction.slide_axis,travel*39.37007874f);
						auto wrist=pose.wrist;wrist.position=add(wrist.position,offset);
						const auto candidate=choose_part_grip({&pose,1},wrist,offset,p->slide_grab_low,p->slide_grab_high,39.37007874f);
						check(candidate.pose==0 && candidate.distance_meters<.001f,"native knife slide hand contacts the same physical slide at rest/lock/full stroke after mirroring");
						check(pose.fingers.size()==15,"co-grasp fingers remain fully articulated with live glove lengths");
					}
				}
			}
	}
}
