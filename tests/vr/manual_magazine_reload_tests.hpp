#pragma once
#include "component/vr/gameplay/weapons/aa12/profile.hpp"
#include "component/vr/gameplay/weapons/famas/profile.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/l86/profile.hpp"
#include "component/vr/gameplay/weapons/p90/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
#include "component/vr/gameplay/weapons/aa12/reload_profile.hpp"
#include "component/vr/gameplay/weapons/l86/reload_profile.hpp"
#include "component/vr/gameplay/weapons/famas/reload_profile.hpp"
#include "component/vr/gameplay/weapons/tavor/reload_profile.hpp"
#include "component/vr/gameplay/weapons/fn2000/reload_profile.hpp"
#include "component/vr/gameplay/weapons/p90/reload_profile.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"

namespace manual_magazine_reload_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace m=w::mechanics;
		using vr::gameplay::hands::scale;
		// Use the actual P90 contacts and mirror adapter: the old 11 cm handle
		// radius captured the authored magazine wrist even after the rear trim.
		for (const auto* d:{&w::p90::physical,&w::p90::arctic})for (auto rear:{vr::hand::left,vr::hand::right})
		{
			using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
			constexpr float units=39.37007874f;const quat basis{0,0,0,1};const bool off=rear==vr::hand::left;
			const auto action=off ? vr::gameplay::hands::pose_mirror::part(d->slide_grips[0],basis) : d->slide_grips[0];
			const auto in_wrist=off ? vr::gameplay::hands::pose_mirror::object_in_wrist(d->magazine_rest,d->magazine_in_wrist,basis) : d->magazine_in_wrist;
			const auto contact=off ? vr::gameplay::hands::pose_mirror::local_point(d->magazine_contacts->grip_contact,basis) : d->magazine_contacts->grip_contact;
			const auto magazine_wrist=compose(d->magazine_rest,inverse(in_wrist));
			for (const bool magazine:{true,false})for (const float degrees:{-30.f,0.f,30.f})for (const vec axis:{vec{1,0,0},vec{0,1,0},vec{0,0,1}})
			{
				const float half=degrees*.00872664626f;const auto v=scale(axis,std::sin(half));
				const quat turn{v[0],v[1],v[2],std::cos(half)};
				auto rotation=multiply(turn,magazine ? magazine_wrist.rotation : action.wrist.rotation);
				check(w::closer_grasp_facing(rotation,magazine_wrist.rotation,action.wrist.rotation)==magazine,
					"P90 top and side grasps remain distinct with 30-degree wrist variation");
				for (auto& x:rotation)x=-x;
				check(w::closer_grasp_facing(rotation,magazine_wrist.rotation,action.wrist.rotation)==magazine,
					"quaternion sign cannot reverse P90 part selection");
			}
			for (bool magazine:{true,false})for (float approach:{-.04f,0.f,.04f})
			{
				Fixture f(d);f.owner.rear=rear;auto wrist=magazine ? magazine_wrist : action.wrist;
				wrist.position[0]+=approach*units;
				const std::array<w::part_grip_pose,1> poses{action};
				const auto candidate=w::choose_part_grip(poses,wrist,{},d->slide_grab_low,d->slide_grab_high,units);
				f.geometry.slide_distance=candidate.distance_meters;f.geometry.slide_pose=candidate.pose;
				f.geometry.magazine=w::magazine_contacts(*d,{{},{0,0,0,1}},wrist,compose(wrist,in_wrist),units,&contact);
				f.geometry.magazine_facing=w::closer_grasp_facing(wrist.rotation,magazine_wrist.rotation,action.wrist.rotation);
				f.geometry.hand_in_gun=scale(wrist.position,1/units);f.step();f.trigger(true);
				check(magazine ? f.control.magazine_grabbed() && !f.control.slide_held() : f.control.slide_held() && !f.control.magazine_grabbed(),
					"P90 authored magazine/handle approaches select their own part in either hand and skin");
			}
			for (const bool magazine_facing:{true,false})
			{
				Fixture f(d);f.owner.rear=rear;
				// Deliberately make the other part's contact closer. Box distance
				// must not override an unambiguous grip facing in this overlap.
				f.geometry.slide_distance=magazine_facing ? .01f : .04f;
				f.geometry.magazine.grip_distance=magazine_facing ? .04f : 0.f;
				f.geometry.magazine_facing=w::closer_grasp_facing(magazine_facing ? magazine_wrist.rotation : action.wrist.rotation,
					magazine_wrist.rotation,action.wrist.rotation);f.step();f.trigger(true);
				check(magazine_facing ? f.control.magazine_grabbed() : f.control.slide_held(),"P90 overlap selects the intended grasp despite the other box being closer");
				f.geometry.magazine_facing=!magazine_facing;f.step();
				check(magazine_facing ? f.control.magazine_grabbed() && !f.control.slide_held() : f.control.slide_held() && !f.control.magazine_grabbed(),
					"crossing a P90 overlap cannot switch an already-held magazine or handle");
			}
			for (const bool magazine:{true,false})
			{
				int overlaps{};
				for (float x:{-.04f,0.f,.04f})for (float y:{-.04f,0.f,.04f})for (float z:{-.04f,0.f,.04f})
				{
					auto wrist=magazine ? magazine_wrist : action.wrist;wrist.position=add(wrist.position,scale({x,y,z},units));
					const std::array<w::part_grip_pose,1> poses{action};
					const auto candidate=w::choose_part_grip(poses,wrist,{},d->slide_grab_low,d->slide_grab_high,units);
					const auto hit=w::magazine_contacts(*d,{{},{0,0,0,1}},wrist,compose(wrist,in_wrist),units,&contact);
					if (candidate.distance_meters>d->interaction.slide_radius || hit.grip_distance>d->interaction.manual_magazine->grab_radius)continue;
					++overlaps;Fixture f(d);f.owner.rear=rear;f.geometry.slide_distance=candidate.distance_meters;
					f.geometry.slide_pose=candidate.pose;f.geometry.magazine=hit;
					f.geometry.magazine_facing=w::closer_grasp_facing(wrist.rotation,magazine_wrist.rotation,action.wrist.rotation);
					f.geometry.hand_in_gun=scale(wrist.position,1/units);f.step();f.trigger(true);
					check(magazine ? f.control.magazine_grabbed() : f.control.slide_held(),
						"actual P90 contact-volume overlaps preserve top versus side grasp intent");
				}
				check(overlaps>0,"P90 overlap regression exercises real intersecting contacts for each grasp");
			}
			{
				Fixture f(d);f.owner.rear=rear;f.geometry.slide_distance=f.geometry.magazine.grip_distance=0;f.step();f.trigger(true);
				check(f.control.slide_held() && !f.control.magazine_grabbed(),"P90 overlap retains action priority without a closer magazine facing");
				f.trigger(false);auto state=f.state;state.magazine_inserted=false;state.magazine_rounds=0;f.adopt(state);f.step();f.trigger(true);
				check(f.control.slide_held() && !f.control.magazine_grabbed(),"absent P90 magazine cannot steal handle acquisition");
			}
			check(!w::closer_grasp_facing({0,0,0,0},magazine_wrist.rotation,action.wrist.rotation),
				"invalid facing cannot claim magazine priority");
		}
		for(const auto* d:{&w::l86::physical,&w::famas::physical,&w::famas::tape,&w::tavor::physical,&w::tavor::digital,&w::fn2000::physical,&w::aa12::physical,&w::p90::physical,&w::p90::arctic})
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			const int capacity=d->ammunition.magazine_capacity;
			const auto pull=[&](Fixture& f) {
				f.geometry.magazine.grip_distance=0;f.geometry.hand_in_gun={};f.trigger(true);
				f.geometry.hand_in_gun=scale(d->interaction.manual_magazine->pull_axis,.051f);f.step();
			};
			const auto insert=[&](Fixture& f) {
				f.geometry.magazine_top_in_well={0,0,-.2f};f.step();
				f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);
			};
			{
				Fixture f(d);f.owner.rear=rear;f.step();const int total=m::total_rounds(f.state);
				f.button(true);f.button(false);
				check(f.commits==0 && f.state.magazine_inserted,"manual-magazine button cannot eject a loaded magazine");
				f.geometry.magazine.grip_distance=0;f.trigger(true);
				f.geometry.hand_in_gun=scale(d->interaction.manual_magazine->pull_axis,.025f);f.step();f.trigger(false);
				check(f.state.magazine_inserted && f.commits==0,"partial manual-magazine pull stays seated");
				pull(f);
				check(!f.state.magazine_inserted && f.state.held_rounds==capacity-1 && f.state.chamber_loaded &&
					f.last_effect==m::effect::magazine_take && m::total_rounds(f.state)==total,"manual-magazine physical pull retains old rounds and chamber without a second dropped magazine");
				insert(f);
				check(f.state.magazine_inserted && f.native.loaded==capacity && m::total_rounds(f.state)==total,"manual-magazine removed magazine requires withdrawal and reinserts without a refill");
				f.geometry.magazine.grip_distance=1;f.geometry.slide_distance=0;f.trigger(true);
				const auto grip=f.control.slide_grip();f.move_slide(grip,d->interaction.full_stroke*.4f);f.move_slide(grip,0);
				check(f.spent==0,"manual-magazine short stroke leaves the live chamber intact");
				f.move_slide(grip,d->interaction.slide_stroke);f.step();f.step();
				check(f.spent==1 && !f.state.chamber_loaded,"manual-magazine held rear stop extracts only once");
				f.move_slide(grip,0);f.trigger(false);
				check(f.state.chamber_loaded && m::total_rounds(f.state)+f.spent==total,"manual-magazine full return feeds once and conserves ammunition");
			}
			for(bool empty:{false,true})for(bool button:{false,true})
			{
				Fixture f(d);f.owner.rear=rear;auto state=f.state;state.reserve_rounds=3*capacity;f.adopt(state);f.step();
				if(empty)
				{
					for(int n=0;n<capacity;++n)
					{
						const auto tx=m::plan(*f.rules,f.state,{m::operation::accepted_shot,f.state.weapon,f.state.instance_generation,f.state.revision,rear,rear});
						check(tx && f.commit(tx),"manual-magazine accepted shot commits");f.state=tx.next;
					}
					check((f.state.action==m::action_state::locked_open)==d->ammunition.last_round_lock && !m::ready(*f.rules,f.state),"manual-magazine last round applies its own follower policy and blocks firing");
					f.button(true);f.button(false);
					check(f.state.magazine_inserted && (f.state.action==m::action_state::locked_open)==d->ammunition.last_round_lock,"empty action retains its follower policy and button cannot eject the magazine");
				}
				const int total=m::total_rounds(f.state);pull(f);f.trigger(false);
				check(!f.state.magazine_inserted && f.state.magazine_hand==vr::hand::none,"manual-magazine old magazine can be released before drawing replacement");
				f.geometry.magazine.grip_distance=1;f.geometry.waist_distance=0;f.trigger(true);insert(f);
				check(f.state.magazine_inserted && f.native.loaded==(empty?capacity:capacity+1) && f.state.chamber_loaded==!empty &&
					m::total_rounds(f.state)==total,"manual-magazine fresh magazine preserves plus one or empty lock without creating rounds");
				if(empty)
				{
					if(button){f.button(true);f.button(false);}
					if(button && !d->ammunition.release_control) check(!f.state.chamber_loaded,"this action has no button-operated empty release");
					if(!button || !d->ammunition.release_control){f.geometry.slide_distance=0;f.trigger(true);f.move_slide(f.control.slide_grip(),d->interaction.slide_stroke);f.trigger(false);}
					check(f.state.action==m::action_state::closed && f.state.chamber_loaded && f.state.magazine_rounds==capacity-1 &&
						m::total_rounds(f.state)==total,"manual-magazine rapid release or full rack chambers exactly one replacement round");
				}
			}
			{
				Fixture f(d);f.owner.rear=rear;auto state=f.state;state.magazine_inserted=false;state.chamber_loaded=false;state.magazine_rounds=0;
				state.reserve_rounds=3*capacity;state.action=d->ammunition.last_round_lock ? m::action_state::locked_open : m::action_state::closed;f.adopt(state);f.step();
				f.button(true);f.button(false);check(f.state.action==m::action_state::closed,"manual-magazine no-magazine release closes empty");
				f.geometry.waist_distance=0;f.trigger(true);insert(f);f.button(true);f.button(false);
				check(f.state.magazine_inserted && !f.state.chamber_loaded,"manual-magazine release button cannot chamber from a closed action");
			}
			{
				Fixture f(d);f.owner.rear=rear;f.step();f.writable=false;pull(f);const int attempts=f.attempts;
				f.writable=true;f.step();check(f.state.magazine_inserted && f.attempts==attempts,"failed manual-magazine pull consumes the grasp instead of retrying a write");
				f.trigger(false);pull(f);const int total=m::total_rounds(f.state);
				check(f.interrupt() && f.state.magazine_hand==vr::hand::none && f.state.chamber_loaded && m::total_rounds(f.state)==total,
					"manual-magazine interruption refunds held old rounds without reattaching the magazine");
			}
			if(!d->interaction.manual_magazine->spare_strike)
			{
				Fixture f(d);f.owner.rear=rear;f.step();f.geometry.waist_distance=0;f.trigger(true);
				f.geometry.magazine.strike->frame.position={-.08f,0,0};f.step();f.geometry.magazine.strike->frame.position={.005f,0,0};f.step();
				check(f.state.magazine_inserted,"pull-only magazines do not inherit spare-magazine unlatching");
			}
		}
	}
}
