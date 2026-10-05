#pragma once
#include "component/vr/gameplay/weapons/dragunov/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/weapons/m82/profile.hpp"
#include "component/vr/gameplay/weapons/wa2000/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/reload_profile.hpp"
#include "component/vr/gameplay/weapons/m82/reload_profile.hpp"
#include "component/vr/gameplay/weapons/wa2000/reload_profile.hpp"
#include "component/vr/gameplay/weapons/dragunov/reload_profile.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"

namespace precision_reload_tests
{
	template <class Fixture, class Check> void run(Check check)
	{
		namespace w = vr::gameplay::weapons;
		namespace m = w::mechanics;
		using vr::gameplay::hands::scale;
		for (auto definition:{&w::m14ebr::physical,&w::m14ebr::arctic,&w::dragunov::physical}) for (auto rear:{vr::hand::left,vr::hand::right}) for (bool empty:{false,true})
		{
			Fixture f(definition);f.owner.rear=rear;f.step();
			if (empty) {auto s=f.state;s.magazine_rounds=0;s.chamber_loaded=false;s.action=m::action_state::locked_open;f.adopt(s);}
			const auto total=m::total_rounds(f.state);
			f.geometry.waist_distance=0;f.trigger(true);
			const auto strike=[&](float x) {f.geometry.magazine.strike->frame.position={x,0,0};f.step();};
			strike(-.08f);strike(.005f);
			check(!f.state.magazine_inserted && f.state.held_rounds==10 && f.state.chamber_loaded==!empty &&
				m::total_rounds(f.state)==total,"M14 latch strike keeps spare, chamber and total rounds in either hand");
			const auto commits=f.commits;strike(.006f);
			check(f.commits==commits,"M14 lingering latch contact cannot repeat removal");
			for(int n=0;n<30;++n)f.step();
			f.geometry.magazine_top_in_well={0,0,-.2f};f.step();
			f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);
			f.button(true);f.button(false);
			check(f.state.magazine_inserted && f.state.chamber_loaded==!empty &&
				(f.state.action==m::action_state::locked_open)==empty,
				"M14 strike reload never creates a button-operated bolt release");
		}
		for (auto rear:{vr::hand::left,vr::hand::right})
		{
			Fixture f(&w::wa2000::physical);f.owner.rear=rear;f.step();
			const auto source=w::wa2000::action_grips[0];
			const auto pose=rear==vr::hand::left ? vr::gameplay::hands::pose_mirror::part(source,{0,0,0,1}) : source;
			const auto candidate=w::choose_part_grip({&pose,1},pose.wrist,{},w::wa2000::action_grab_low,w::wa2000::action_grab_high,39.37007874f);
			f.geometry.hand_in_gun=scale(pose.wrist.position,.0254f);
			f.geometry.slide_distance=candidate.distance_meters;f.geometry.slide_pose=candidate.pose;f.trigger(true);
			const auto grip=f.control.slide_grip();
			check(f.control.slide_held() && grip.pose==0,"WA2000 each operating hand acquires its own symmetric handle through production geometry");
			f.geometry.slide_pose=w::no_part_grip;f.move_slide(grip,w::wa2000::action_stroke_m);f.trigger(false);
			check(f.spent==1 && f.state.chamber_loaded && f.native.loaded==9,
				"WA2000 held handle survives candidate changes and extracts exactly once");
		}
		for (auto definition : {&w::m14ebr::physical, &w::m14ebr::arctic, &w::m82::physical, &w::wa2000::physical,&w::dragunov::physical})
			for (auto rear : {vr::hand::left, vr::hand::right})
			{
				const auto capacity = definition->ammunition.magazine_capacity;
				const auto stroke = definition->interaction.slide_stroke;
				const auto axis = definition->interaction.manual_magazine->pull_axis;
				const bool locks = definition != &w::m82::physical;
				const bool releases = definition == &w::wa2000::physical;
				const auto remove = [&](Fixture &f) {
					f.geometry.hand_in_gun = {};
					f.geometry.magazine.grip_distance = 0;
					f.trigger(true);
					f.geometry.hand_in_gun = scale(axis, .051f);
					f.step();
				};
				const auto insert = [](Fixture &f) {
					f.geometry.magazine_top_in_well = {0, 0, -.2f};
					f.step();
					f.geometry.magazine_top_in_well = {0, 0, -.04f};
					f.step();
				};
				{
					Fixture f(definition);
					f.owner.rear = rear;
					f.step();
					const auto total = m::total_rounds(f.state);
					f.button(true);
					f.button(false);
					check(f.commits == 0 && f.state.magazine_inserted,
						  "precision rifle button cannot eject a magazine");
					f.geometry.magazine.grip_distance = 0;
					f.trigger(true);
					f.geometry.hand_in_gun = scale(axis, .025f);
					f.step();
					f.trigger(false);
					check(f.commits == 0 && f.state.magazine_inserted, "precision partial pull leaves magazine seated");
					remove(f);
					check(!f.state.magazine_inserted && f.state.held_rounds == capacity - 1 && f.state.chamber_loaded &&
							  f.native.loaded == 1 && m::total_rounds(f.state) == total,
						  "precision extraction transfers old rounds and preserves chamber");
					insert(f);
					check(f.state.magazine_inserted && f.native.loaded == capacity && m::total_rounds(f.state) == total,
						  "precision reinsertion never refills a partial magazine");
					f.trigger(false);
					f.geometry.magazine.grip_distance = 1;
					f.geometry.slide_distance = 0;
					f.trigger(true);
					f.move_slide(f.control.slide_grip(), stroke * .5f);
					f.trigger(false);
					check(f.spent == 0, "precision partial handle stroke cannot extract a live round");
					f.trigger(true);
					const auto grip = f.control.slide_grip();
					f.move_slide(grip, stroke);
					f.step();
					f.move_slide(grip, 0);
					f.trigger(false);
					check(f.spent == 1 && f.state.chamber_loaded && m::total_rounds(f.state) + f.spent == total,
						  "precision full handle cycle extracts and feeds exactly once");
				}
				{
					Fixture f(definition);
					f.owner.rear = rear;
					f.step();
					for (int n = 0; n < capacity; ++n)
					{
						const auto tx = m::plan(*f.rules, f.state,
												{m::operation::accepted_shot, f.state.weapon,
												 f.state.instance_generation, f.state.revision, rear, rear});
						check(tx && f.commit(tx), "precision shot commits independently of holding hand");
						f.state = tx.next;
					}
					check(!m::ready(*f.rules, f.state) && (f.state.action == m::action_state::locked_open) == locks,
						  "precision last round applies the weapon-specific hold-open rule");
					remove(f);
					f.trigger(false);
					f.geometry.magazine.grip_distance = 1;
					check(!f.state.magazine_inserted, "precision empty magazine also requires physical extraction");
					f.geometry.waist_distance = 0;
					f.trigger(true);
					const auto total = m::total_rounds(f.state);
					insert(f);
					f.trigger(false);
					check(f.state.magazine_inserted && !f.state.chamber_loaded,
						  "precision replacement insertion cannot chamber by itself");
					f.button(true);
					f.button(false);
					check(f.state.chamber_loaded == releases && f.state.magazine_inserted,
						  "only WA2000 button releases the empty lock");
					if (!releases)
					{
						f.geometry.waist_distance = 1;
						f.geometry.slide_distance = 0;
						f.trigger(true);
						f.move_slide(f.control.slide_grip(), stroke);
						f.trigger(false);
					}
					check(f.state.chamber_loaded && f.state.action == m::action_state::closed &&
							  f.state.magazine_rounds == capacity - 1 && m::total_rounds(f.state) == total,
						  "precision empty reload feeds exactly one from replacement magazine");
				}
				{
					Fixture f(definition);
					f.owner.rear = rear;
					f.step();
					f.writable = false;
					remove(f);
					check(f.state.magazine_inserted && f.native.loaded == capacity,
						  "failed precision removal transaction preserves the seated magazine");
					f.trigger(false);
					f.writable = true;
					remove(f);
					check(!f.state.magazine_inserted && f.state.held_rounds == capacity - 1,
						  "precision removal retries on a fresh grip");
				}
			}
	}
} // namespace precision_reload_tests
