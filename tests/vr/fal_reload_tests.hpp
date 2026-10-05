#pragma once
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/fal/reload_profile.hpp"

namespace fal_reload_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons; namespace m=w::mechanics;
		using vr::gameplay::hands::scale;
		const auto strike=[](Fixture& f,float x,float y=0.f) {
			f.geometry.magazine.strike->frame.position={x,y,0};f.step();
		};
		for (auto rear:{vr::hand::right,vr::hand::left})
		{
			Fixture f(&w::fal::physical);f.owner.rear=rear;f.step();const auto total=m::total_rounds(f.state);
			f.button(true);f.button(false);
			check(f.commits==0 && f.state.magazine_inserted,"FAL release button cannot remove a loaded magazine");
			f.geometry.magazine.grip_distance=0;f.trigger(true);
			f.geometry.hand_in_gun=scale(w::fal::manual_magazine.pull_axis,.025f);f.step();f.trigger(false);
			check(f.state.magazine_inserted && f.commits==0,"partial FAL pull leaves the magazine seated");
			f.geometry.hand_in_gun={};f.trigger(true);
			f.geometry.hand_in_gun=scale(w::fal::manual_magazine.pull_axis,.051f);f.step();
			check(!f.state.magazine_inserted && f.state.held_rounds==19 && f.state.chamber_loaded &&
				f.last_effect==m::effect::magazine_take && m::total_rounds(f.state)==total,"FAL hand removal owns the old magazine and preserves chamber");
			f.geometry.magazine_top_in_well={0,0,-.2f};f.step();f.geometry.magazine_top_in_well={0,0,-.04f};f.step();
			check(f.state.magazine_inserted && f.native.loaded==20 && m::total_rounds(f.state)==total,"FAL reinserts the same partial magazine without refilling it");
			f.trigger(false);f.geometry.magazine.grip_distance=1;f.geometry.slide_distance=0;f.trigger(true);
			const auto grip=f.control.slide_grip();f.move_slide(grip,.06f);f.trigger(false);
			check(f.spent==0,"FAL short charging-handle stroke cannot extract a live round");
			f.trigger(true);const auto full=f.control.slide_grip();f.move_slide(full,.144f);f.step();f.move_slide(full,0);
			check(f.spent==1 && f.state.chamber_loaded && m::total_rounds(f.state)+f.spent==total,"FAL held rear stop extracts once and forward return feeds once");
		}
		for (auto rear:{vr::hand::right,vr::hand::left}) for (bool empty:{false,true}) for (bool button:{false,true})
		{
			Fixture f(&w::fal::physical);f.owner.rear=rear;f.step();
			if (empty)
			{
				for (int n=0;n<20;++n)
				{
					const auto tx=m::plan(*f.rules,f.state,{m::operation::accepted_shot,f.state.weapon,f.state.instance_generation,f.state.revision,rear,rear});
					check(tx && f.commit(tx),"FAL accepted shot commits");f.state=tx.next;
				}
				check(f.state.action==m::action_state::locked_open && !m::ready(*f.rules,f.state),"last FAL round holds the feed open");
				f.button(true);f.button(false);
				check(f.state.magazine_inserted && f.state.action==m::action_state::locked_open,"empty follower prevents release but button still cannot eject");
			}
			const auto total=m::total_rounds(f.state);f.geometry.waist_distance=0;f.trigger(true);strike(f,-.08f);strike(f,.005f);
			check(!f.state.magazine_inserted && f.state.held_rounds==20 && f.last_effect==m::effect::magazine_out &&
				m::total_rounds(f.state)==total,"FAL spare impact ejects only the old magazine and keeps the spare in escrow");
			const auto commits=f.commits;strike(f,.006f);strike(f,.005f);
			check(f.commits==commits,"continuing FAL contact cannot repeat ejection");
			for(int n=0;n<30;++n)f.step();
			f.geometry.magazine_top_in_well={0,0,-.2f};f.step();f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);
			check(f.state.magazine_inserted && f.native.loaded==(empty ? 20 : 21) && f.state.chamber_loaded==!empty,
				"FAL spare insertion preserves tactical chamber or empty lock");
			if (empty)
			{
				if (button) {f.button(true);f.button(false);}
				else {f.geometry.slide_distance=0;f.trigger(true);f.move_slide(f.control.slide_grip(),.144f);f.trigger(false);}
				check(f.state.action==m::action_state::closed && f.state.chamber_loaded && f.state.magazine_rounds==19 &&
					m::total_rounds(f.state)==total,"FAL button or full handle cycle chambers exactly one from replacement magazine");
			}
		}
		{
			Fixture f(&w::fal::physical);auto s=f.state;s.magazine_inserted=false;s.chamber_loaded=false;s.magazine_rounds=0;s.action=m::action_state::locked_open;f.adopt(s);
			f.button(true);f.button(false);check(f.state.action==m::action_state::closed && !f.state.chamber_loaded,"FAL no-magazine release closes empty");
			f.geometry.waist_distance=0;f.trigger(true);f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);f.button(true);
			check(!f.state.chamber_loaded && f.state.magazine_inserted,"FAL button cannot chamber from an already closed action or remove the new magazine");
		}
		for (int mode=0;mode<4;++mode)
		{
			Fixture f(&w::fal::physical);f.geometry.waist_distance=0;f.trigger(true);
			if (mode==0) {strike(f,.005f);strike(f,.006f);}
			if (mode==1) {strike(f,.08f);strike(f,-.005f);}
			if (mode==2) {strike(f,-.08f,.2f);strike(f,.005f,.2f);}
			if (mode==3) {strike(f,-.5f);strike(f,.005f);}
			check(f.state.magazine_inserted && f.commits==1,"FAL overlap, wrong direction, missed paddle and tracking jump do not unlatch");
		}
		{
			Fixture f(&w::fal::physical);f.geometry.waist_distance=0;f.trigger(true);strike(f,-.08f);f.writable=false;strike(f,.005f);
			const auto attempts=f.attempts;f.writable=true;strike(f,.006f);
			check(f.state.magazine_inserted && f.attempts==attempts,"failed FAL strike transaction requires a new separated approach");
			check(f.interrupt() && f.state.magazine_hand==vr::hand::none && f.state.reserve_rounds==60,"interrupted FAL strike refunds only the held spare");
		}
	}
}
