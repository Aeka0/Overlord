#pragma once
#include "component/vr/gameplay/weapons/pp2000/profile.hpp"
#include "component/vr/gameplay/weapons/pp2000/reload_profile.hpp"

namespace pp2000_reload_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace m=w::mechanics;
		for (auto rear:{vr::hand::right,vr::hand::left})
		{
			Fixture f(&w::pp2000::physical);f.owner.rear=rear;f.step();
			for (int n=0;n<20;++n)
			{
				const auto tx=m::plan(*f.rules,f.state,{m::operation::accepted_shot,f.state.weapon,f.state.instance_generation,f.state.revision,rear,rear});
				check(tx && f.commit(tx),"PP2000 native accepted shot commits once");f.state=tx.next;
			}
			check(f.native.loaded==0 && !f.state.chamber_loaded && f.state.action==m::action_state::closed && !m::ready(*f.rules,f.state),
				"PP2000 last shot leaves action forward with no follower lock");
			f.button(true);f.button(false);check(!f.state.magazine_inserted && f.state.action==m::action_state::closed,"PP2000 empty magazine is button-released without changing action state");
			const auto commits=f.commits;f.button(true);f.button(false);check(f.commits==commits,"PP2000 repeated release with no magazine cannot operate the bolt");
			f.geometry.waist_distance=0;f.trigger(true);f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);
			check(f.native.loaded==20 && !f.state.chamber_loaded && !m::ready(*f.rules,f.state),"PP2000 empty replacement does not invent a chambered round");
			const auto total=m::total_rounds(f.state);const int fired=f.spent;f.geometry.slide_distance=0;f.trigger(true);
			f.move_slide(f.control.slide_grip(),.03f);f.trigger(false);
			check(!f.state.chamber_loaded,"short PP2000 rack cannot finish an empty reload");
			f.trigger(true);const auto grip=f.control.slide_grip();f.move_slide(grip,.065f);f.step();f.trigger(false);
			check(f.state.chamber_loaded && f.state.magazine_rounds==19 && f.spent==fired && m::total_rounds(f.state)==total,
				"full PP2000 handle return chambers one without spending an absent round");
			f.trigger(true);const auto live=f.control.slide_grip();f.move_slide(live,.065f);f.step();f.step();f.trigger(false);
			check(f.spent==fired+1 && f.state.chamber_loaded && m::total_rounds(f.state)+f.spent-fired==total,"held PP2000 rear stop extracts a live chamber only once");
		}
		{
			Fixture f(&w::pp2000::physical);const auto total=m::total_rounds(f.state);
			f.geometry.waist_distance=0;f.trigger(true);f.button(true);f.button(false);
			f.geometry.magazine_top_in_well={0,0,-.04f};f.step();
			check(f.native.loaded==21 && f.state.chamber_loaded && f.state.magazine_rounds==20 && m::total_rounds(f.state)==total,
				"PP2000 tactical button reload preserves twenty plus one");
		}
		{
			Fixture f(&w::pp2000::physical);f.writable=false;f.button(true);const auto attempts=f.attempts;f.step();f.writable=true;f.step();
			check(f.state.magazine_inserted && f.attempts==attempts,"failed PP2000 release cannot repeat a native write on a held button");
			f.button(false);f.button(true);check(!f.state.magazine_inserted,"fresh PP2000 button press retries after rejection");
		}
		{
			Fixture f(&w::pp2000::physical);f.geometry.waist_distance=0;f.trigger(true);f.button(true);f.button(false);
			const auto total=m::total_rounds(f.state);f.interrupt();
			check(f.state.magazine_hand==vr::hand::none && m::total_rounds(f.state)==total,"PP2000 tracking/switch interruption returns held spare escrow");
		}
	}
}
