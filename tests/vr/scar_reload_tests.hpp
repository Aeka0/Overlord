#pragma once
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/scar/reload_profile.hpp"

namespace scar_reload_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace m=w::mechanics;
		for(auto rear:{vr::hand::left,vr::hand::right})for(bool empty:{false,true})for(bool use_handle:{false,true})
		{
			Fixture f(&w::scar::physical);f.owner.rear=rear;f.step();
			if(empty)
			{
				for(int n=0;n<20;++n)
				{
					const auto tx=m::plan(*f.rules,f.state,{m::operation::accepted_shot,f.state.weapon,f.state.instance_generation,f.state.revision,rear,rear});
					check(tx && f.commit(tx),"SCAR twenty accepted rifle shots commit");f.state=tx.next;
				}
				f.step();check(f.state.action==m::action_state::locked_open && f.control.slide_travel()==f.tuning->locked_travel,"SCAR empty follower retains reciprocating handle rearward");
			}
			const auto total=m::total_rounds(f.state);
			f.geometry.waist_distance=0;f.trigger(true);f.button(true);f.button(false);
			check(!f.state.magazine_inserted && f.state.held_rounds==20 && f.state.chamber_loaded==!empty,"SCAR button eject preserves chamber and held spare");
			f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);
			check(f.state.magazine_inserted && f.native.loaded==(empty?20:21) && m::total_rounds(f.state)==total,"SCAR insertion conserves ammo and supports tactical twenty plus one");
			if(empty)
			{
				check(!m::ready(*f.rules,f.state),"SCAR inserted magazine does not bypass follower lock");
				if(use_handle){f.geometry.slide_distance=0;f.trigger(true);const auto grip=f.control.slide_grip();f.move_slide(grip,f.tuning->slide_stroke);f.move_slide(grip,0);f.trigger(false);}
				else {f.button(true);f.button(false);}
				check(m::ready(*f.rules,f.state) && f.state.chamber_loaded && f.native.loaded==20 && m::total_rounds(f.state)==total,"SCAR release button or handle overpull feeds one round after empty reload");
			}
			f.geometry.slide_distance=0;f.trigger(true);const auto grip=f.control.slide_grip();const auto spent=f.spent;
			f.move_slide(grip,.02f);f.move_slide(grip,0);check(f.spent==spent,"SCAR partial handle pull does not eject a chamber round");
			f.move_slide(grip,f.tuning->slide_stroke);f.step(false);f.move_slide(grip,0);f.trigger(false);
			check(f.spent==spent+1 && f.state.chamber_loaded && m::total_rounds(f.state)+1==total,"SCAR full cycle extracts exactly once and feeds next rifle round");
		}
	}
}
