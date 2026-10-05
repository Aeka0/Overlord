#pragma once
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/m1014/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/weapons/ump/reload_profile.hpp"
#include "component/vr/gameplay/weapons/cheytac/reload_profile.hpp"
#include "component/vr/gameplay/weapons/mp5/reload_profile.hpp"
#include "component/vr/gameplay/tube_presenter.hpp"
#include "component/vr/gameplay/part_return_transition.hpp"
#include "component/vr/gameplay/empty_hand_pose.hpp"

namespace comfort_interaction_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace m=w::mechanics;namespace p=w::physical_reload;
		using namespace vr::gameplay::hands;using namespace std::chrono_literals;
		for(const auto* d:{&w::ump::physical,&w::ump::arctic,&w::ump::digital})for(auto rear:{vr::hand::left,vr::hand::right})
		{
			const auto prepare=[&](Fixture& f){
				f.owner.rear=rear;auto s=f.state;s.magazine_rounds=0;s.chamber_loaded=true;s.action=m::action_state::closed;f.adopt(s);
				const auto shot=m::plan(*f.rules,f.state,{m::operation::accepted_shot,s.weapon,s.instance_generation,s.revision,rear,rear});
				check(shot && f.commit(shot),"UMP last shot uses the real ammo transaction");if(shot)f.state=shot.next;f.step();
				check(f.state.action==m::action_state::locked_open,"UMP empty follower holds bolt without raising charging handle");
				f.geometry.magazine.grip_distance=0;f.trigger(true);f.geometry.hand_in_gun=scale(f.tuning->manual_magazine->pull_axis,.06f);f.step();f.trigger(false);
				f.geometry.magazine.grip_distance=1;f.geometry.waist_distance=0;f.trigger(true);f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);
				check(f.state.magazine_inserted && f.state.magazine_hand==vr::hand::none && f.state.magazine_rounds==25 && f.state.action==m::action_state::locked_open,"UMP manually replaces empty magazine before side release");
				f.geometry.waist_distance=1;f.geometry.catch_input.valid=true;
			};
			const auto sweep=[](Fixture& f,bool side){for(float distance:{.12f,.065f,.015f}){
				const vec point=side?vec{0,distance,0}:vec{0,0,distance};f.geometry.catch_input.slap_points.fill(point);f.geometry.catch_input.hand_world=point;f.step();}};
			for(int mode=0;mode<5;++mode)
			{
				Fixture f(d);prepare(f);const auto total=m::total_rounds(f.state);const auto loaded=f.native.loaded;
				if(mode==1){auto s=f.state;s.action=m::action_state::latched_open;f.adopt(s);}
				if(mode==2)f.writable=false;
				if(mode==3)f.owner.support=vr::hand(1-int(rear));
				if(mode==4)f.trigger(true);
				const auto phase=f.state.action;const int commits=f.commits;sweep(f,true);
				check(f.commits==commits+int(mode==0),"side slap requires free hand, lower handle and accepted native compare");
				check(m::total_rounds(f.state)==total && f.native.loaded==loaded,"side release neither creates nor consumes ammunition");
				if(mode==0){check(m::ready(*f.rules,f.state) && f.state.magazine_rounds==24,"side catch feeds exactly one round");f.step(false);check(f.commits==commits+1,"duplicate input cannot release twice");}
				else check(f.state.action==phase,"rejected side release preserves action state");
				if(mode==1){sweep(f,false);check(m::ready(*f.rules,f.state),"upper HK slap still works after rejected side slap; histories do not reset each other");}
				if(mode==2){f.writable=true;sweep(f,true);check(m::ready(*f.rules,f.state),"failed side release can retry after a fresh separated approach");}
			}
			const auto lower=w::action_slap_centre(*d,m::action_state::locked_open,39.37007874f),upper=w::action_slap_centre(*d,m::action_state::latched_open,39.37007874f);
			check(length(sub(lower,upper))>4 && lower[1]>0 && upper[1]>0,"follower and raised-handle impacts use distinct targets on the actual left receiver");
		}
		for(const auto* d:{&w::cheytac::physical,&w::cheytac::desert,&w::mp5::physical,&w::mp5::arctic})for(auto rear:{vr::hand::left,vr::hand::right})
		{
			check(d->interaction.manual_magazine->spare_strike && d->magazine_contacts->strike_regions.size()==1,"M200 and MP5K expose the complete spare-magazine body");
			Fixture f(d);f.owner.rear=rear;f.step();f.geometry.waist_distance=0;f.trigger(true);
			const auto held=f.state.held_rounds;const auto initial=m::total_rounds(f.state);const auto chamber=f.state.chamber_loaded;
			f.geometry.magazine.strike->frame.position={-.09f,0,0};f.step();f.geometry.magazine.strike->frame.position={.01f,0,0};f.step();
			check(!f.state.magazine_inserted && f.state.magazine_hand==vr::hand(1-int(rear)) && f.state.held_rounds==held,"spare strike releases installed magazine while retaining the spare");
			check(f.state.chamber_loaded==chamber && m::total_rounds(f.state)+f.spent==initial,"M200/MP5K latch removal preserves chamber and ammunition disposition");
			const int commits=f.commits;f.step(false);f.step();check(f.commits==commits,"sustained latch overlap cannot repeat magazine removal");
		}
		for(const auto* d:{&w::m1014::feed,&w::m1014::arctic_feed})
		{
			p::part_return_transition spring;const auto start=p::clock::time_point{1s};const float locked=d->interaction.rack.locked_travel;
			spring.update(1,1,false,locked,start,d->bolt_return_seconds);
			check(spring.update(1,1,false,0,start+10ms,d->bolt_return_seconds)==locked,"port-load close starts at the previously open bolt instead of snapping");
			const auto halfway=spring.update(1,1,false,0,start+60ms,d->bolt_return_seconds);
			check(halfway>0 && halfway<locked,"M1014 bolt has visible intermediate return positions");
			check(spring.update(1,1,false,0,start+110ms,d->bolt_return_seconds)==0,"M1014 reaches exact closed endpoint in its bounded transition");
			check(spring.update(2,1,false,0,start+120ms,d->bolt_return_seconds)==0,"a new weapon instance cannot inherit an old return");
		}
		for(auto holder:{vr::hand::left,vr::hand::right})for(unsigned style=0;style<2;++style)
		{
			w::tube::presentation v;v.active=true;v.owner={42,1,vr::hand::none,holder,w::hold_source::interaction,1};v.rack_held=true;v.rack_grip.pose=static_cast<std::uint8_t>(style);
			const auto actor=vr::hand(1-int(holder));check(w::tube::interaction_hand(v)==actor,"support-only M1014 reports the other hand's rack lease for both styles");
			rig r{};r.count=2;r.parent.fill(-1);r.arms[int(actor)].wrist=int(actor);std::array<bone,2> pose{};for(auto& b:pose)b.rotation={0,0,0,1};
			const auto before=pose[int(actor)].rotation;const auto rotation=normalize(quat{.3f,.1f,.2f,1});
			check(!empty_hand::orient_wrist(r,actor,{},true,rotation,{0,0,0,1},pose) && pose[int(actor)].rotation==before,"posed-hand protection preserves rack wrist before the central lease is published");
			v.owner.rear=holder;v.owner.support=actor;check(w::tube::interaction_hand(v)==vr::hand::none,"pump support remains owned by carry instead of duplicating a part lease");
		}
	}
}
