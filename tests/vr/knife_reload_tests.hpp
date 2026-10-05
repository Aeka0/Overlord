#pragma once
#include "component/vr/gameplay/weapons/de50/profile.hpp"
#include "component/vr/gameplay/weapons/m1911/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/m93r/profile.hpp"
#include "component/vr/gameplay/weapons/usp/profile.hpp"
#include "component/vr/gameplay/knife_profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"

namespace knife_reload_tests
{
	template<class Fixture,class Check> void run(Check&& check)
	{
		using namespace vr::gameplay;
		using namespace weapons;
		using namespace hands;
		using namespace vr::gameplay::hands::pose_math;
		for (const auto* definition:reload_profiles)
			check(bool(definition->knife_magazine_in_wrist)==(definition==&usp::physical || definition==&usp::silenced_physical ||
				definition==&m9::physical || definition==&m1911::physical || definition==&de50::physical || definition==&m93r::physical),
				"knife co-grasp capability stays limited to the five authored pistol families");
		for (const auto* definition:{&usp::physical,&usp::silenced_physical,&m9::physical,&m1911::physical,&de50::physical,&m93r::physical})
			for (const auto rear:{hand::left,hand::right})
				for (const auto mode:{equipment::knife_grip::forward,equipment::knife_grip::reverse})
					for (bool spare_first:{false,true})
					for (bool return_knife:{false,true})
					{
						Fixture f(definition);f.owner.rear=rear;f.step();
						const auto off=hand(1-int(rear));equipment::knife_state knife;knife.take(off,mode);
						f.input.squeeze[int(off)]={true,true,1,1};f.geometry.knife_held=true;
						const auto total=mechanics::total_rounds(f.state);
						if (!spare_first) {f.button(true);f.button(false);}
						f.geometry.waist_distance=.01f;f.geometry.slide_distance=1;
						f.trigger(true);
						check(f.state.magazine_hand==off && !f.control.slide_held() && f.control.knife_magazine_grasp(),
							"new waist pinch draws a magazine with knife Grip held");
						const auto commits=f.commits;f.step(false);f.step();
						check(f.commits==commits && knife.holder==off,"duplicate frames cannot duplicate magazines or return the knife");
						if (spare_first) {f.button(true);f.button(false);}
						// Returning only the knife must not cancel Trigger ownership or change the grasp.
						if (return_knife) {knife.release(1u<<int(off));f.input.squeeze[int(off)].down=false;f.geometry.knife_held=false;f.step();}
						physical_reload::presentation view;view.active=true;view.definition=definition;view.ammo=f.state;
						view.knife_magazine_grasp=f.control.knife_magazine_grasp();
						check(view.use_knife_magazine_grasp(false) && f.state.magazine_hand==off && knife.holder==(return_knife ? hand::none : off),
							"Grip release returns only the knife and retains the magazine's co-grasp until Trigger release");
						f.geometry.magazine_top_in_well={0,0,-.1f};f.step();
						f.geometry.magazine_top_in_well={0,0,0};f.step();
						check(f.state.magazine_inserted && f.state.magazine_hand==hand::none && f.control.magazine_seated(),
							"knife-drawn spare seats through ordinary pistol contact in either reload order");
						f.trigger(false);check(!f.control.magazine_seated(),"Trigger release ends seating independently of Grip");
						check(knife.holder==(return_knife ? hand::none : off),"Trigger release never returns a knife whose Grip remains down");
						check(mechanics::total_rounds(f.state)+f.spent==total,"knife reload preserves the native ammunition total");
						view.ammo=f.state;view.magazine_seated=false;
						check(!view.use_knife_magazine_grasp(false),"completed co-grasp does not leak into the next ordinary magazine");
					}
		for (const auto* definition:{&usp::physical,&m9::physical,&m1911::physical,&de50::physical,&m93r::physical})
		{
			for (bool knife:{false,true}) for (auto rear:{hand::left,hand::right}) for (float degrees:{86.f,90.f,94.f,96.f,180.f})
			{
				Fixture loose(definition);loose.owner.rear=rear;loose.step();loose.geometry.knife_held=knife;
				loose.button(true);loose.button(false);loose.geometry.waist_distance=0;loose.trigger(true);
				loose.geometry.insertion_alignment=std::cos(degrees*3.14159265359f/180.f);
				loose.geometry.magazine_top_in_well={0,0,-.10f};loose.step();
				loose.geometry.magazine_top_in_well={0,0,.085f};loose.step();
				check(loose.state.magazine_inserted==(degrees<95.f),"bare/knife hands accept the expanded upper volume and 95-degree angle but reject reversed magazines");
			}
			Fixture f(definition);f.geometry.knife_held=true;f.geometry.slide_distance=1;f.trigger(true);
			check(!f.control.slide_held() && f.state.magazine_hand==hand::none,"knife cannot acquire a part outside its contact region");
			f.geometry.waist_distance=0;f.step();check(f.state.magazine_hand==hand::none,"holding Trigger then entering supply is not a fresh grab");
			f.trigger(false);f.trigger(true);const auto total=mechanics::total_rounds(f.state);
			f.input.focused=false;f.step();check(f.state.magazine_hand==hand::none && mechanics::total_rounds(f.state)==total,
				"focus interruption refunds co-held escrow once");
			f.input.focused=true;f.step();check(f.state.magazine_hand==hand::none,"focus restoration cannot replay a held Trigger");
			f.trigger(false);f.trigger(true);++f.input.reference_generation;++f.geometry.reference_generation;f.step();
			check(f.state.magazine_hand==hand::none && mechanics::total_rounds(f.state)==total,"recenter cancels co-grasp without duplicating reserve");
			f.trigger(false);f.trigger(true);f.input.grip[0].valid=false;f.step();
			check(f.state.magazine_hand==hand::none,"tracking loss cancels the magazine lease while knife return still requires Grip release");
			Fixture empty(definition);auto state=empty.state;state.magazine_rounds=0;state.chamber_loaded=false;
			state.action=mechanics::action_state::locked_open;empty.adopt(state);empty.geometry.knife_held=true;
			empty.button(true);empty.button(false);empty.geometry.waist_distance=0;empty.trigger(true);
			empty.geometry.magazine_top_in_well={0,0,0};empty.step();empty.trigger(false);empty.button(true);
			check(empty.state.chamber_loaded && empty.state.action==mechanics::action_state::closed &&
				empty.native.loaded==definition->ammunition.magazine_capacity,"empty knife reload uses the rear-hand slide release to chamber normally");
			Fixture rejected(definition);rejected.geometry.knife_held=true;rejected.geometry.waist_distance=0;
			rejected.writable=false;rejected.trigger(true);
			check(rejected.state.magazine_hand==hand::none && !rejected.control.knife_magazine_grasp(),"rejected draw cannot publish a co-grasp");
			rejected.writable=true;rejected.step();check(rejected.state.magazine_hand==hand::none,"rejected draw consumes its Trigger edge");
			rejected.trigger(false);rejected.trigger(true);rejected.writable=false;
			check(!rejected.interrupt() && rejected.control.knife_magazine_grasp() && rejected.state.magazine_hand!=hand::none,
				"rejected refund retains both escrow and its original magazine grasp");
			rejected.writable=true;check(rejected.interrupt() && rejected.state.magazine_hand==hand::none,"accepted refund ends the retained co-grasp exactly once");
		}
		// Native source witness, independent of the promoted runtime constants.
		const vec source_palm{2.98785378f,.56623006f,1.04041961f};
		for (const quat basis:{quat{0,0,0,1},normalize(quat{.31f,-.27f,.52f,.73f})})
			for (bool right:{false,true})
			{
				const auto forward=equipment::knife_profile::attachment(equipment::knife_grip::forward,basis,right,equipment::knife_hand_pose::magazine);
				const auto reverse=equipment::knife_profile::attachment(equipment::knife_grip::reverse,basis,right,equipment::knife_hand_pose::magazine);
				const auto expected_palm=right ? vr::gameplay::hands::pose_mirror::local_point(source_palm,basis) : source_palm;
				check(length(sub(compose(reverse,{equipment::knife_profile::grip_center,{0,0,0,1}}).position,expected_palm))<.0001f,
					"knife co-grasp retains the native USP frame-15 handle contact");
				check(length(sub(compose(forward,{equipment::knife_profile::grip_center,{0,0,0,1}}).position,
					compose(reverse,{equipment::knife_profile::grip_center,{0,0,0,1}}).position))<.0001f,
					"co-grasp forward/reverse preserves the same palm contact in either anatomical hand");
				check(dot(rotate(forward.rotation,{1,0,0}),rotate(reverse.rotation,{1,0,0}))<-.999f,
					"co-grasp flip changes only the blade direction");
				for (const auto* p:{&usp::physical,&m9::physical,&m1911::physical,&de50::physical,&m93r::physical})
				{
					const anchor gun{{210,-100,35},normalize({.2f,-.6f,.3f,.7f})};
					auto mag=compose(gun,p->magazine_rest);
					const auto well=compose(gun,p->well);const vec tip{.005f,-.004f,.02f};
					mag.position=sub(compose(well,{scale(tip,39.37007874f),{0,0,0,1}}).position,rotate(mag.rotation,p->magazine_top));
					const auto grip=right ? vr::gameplay::hands::pose_mirror::object_in_wrist(p->magazine_rest,*p->knife_magazine_in_wrist,basis) : *p->knife_magazine_in_wrist;
					if (p==&de50::physical)
					{
						const auto reference=right ? vr::gameplay::hands::pose_mirror::object_in_wrist(usp::physical.magazine_rest,*usp::physical.knife_magazine_in_wrist,basis) : *usp::physical.knife_magazine_in_wrist;
						check(dot(rotate(grip.rotation,{0,0,1}),rotate(reference.rotation,{0,0,1}))>.99999f &&
							dot(rotate(grip.rotation,{1,0,0}),rotate(reference.rotation,{1,0,0}))>.99999f,
							"DE50 knife magazine matches the common grasp rail and roll in both anatomical hands");
					}
					const auto wrist=compose(mag,inverse(grip));const auto visible=compose(wrist,grip);
					check(length(sub(visible.position,mag.position))<.0001f && magazine_alignment(*p,gun,visible)>.9999f &&
						length(sub(magazine_tip_in_well(*p,gun,visible,39.37007874f),tip))<.0001f,
						"co-grasp insertion geometry and mirrored visible magazine agree including the tilted Desert Eagle rail");
				}
			}
	}
}
