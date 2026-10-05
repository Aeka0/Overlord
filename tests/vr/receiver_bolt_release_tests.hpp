#pragma once
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/vector/profile.hpp"
#include "component/vr/gameplay/weapons/m4/reload_profile.hpp"
#include "component/vr/gameplay/weapons/m16/reload_profile.hpp"
#include "component/vr/gameplay/weapons/scar/reload_profile.hpp"
#include "component/vr/gameplay/weapons/vector/reload_profile.hpp"

namespace receiver_bolt_release_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace m=w::mechanics;
		for(const auto* definition:{&w::m4::physical,&w::m16::physical,&w::scar::physical,&w::vector::physical,&w::vector::black})
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			const auto prepare=[&](Fixture& f) {
				f.owner.rear=rear;auto s=f.state;s.chamber_loaded=false;s.magazine_rounds=0;s.action=m::action_state::locked_open;f.adopt(s);f.step();
				f.geometry.waist_distance=0;f.trigger(true);f.button(true);f.button(false);
				f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);
				check(f.state.magazine_inserted && f.state.magazine_hand==vr::hand::none && f.state.action==m::action_state::locked_open,
					"receiver slap starts after actual empty-magazine replacement has completed");
				f.geometry.waist_distance=1;f.geometry.catch_input.valid=true;
			};
			const auto point=[](Fixture& f,float y,bool world=true,bool one=false) {
				f.geometry.catch_input.slap_points.fill(one?w::physical_reload::vec{.4f,0,0}:w::physical_reload::vec{0,y,0});
				if(one)f.geometry.catch_input.slap_points[5]={0,y,0};
				if(world)f.geometry.catch_input.hand_world={0,y,0};f.step();
			};
			for(int mode=0;mode<18;++mode)
			{
				Fixture f(definition);prepare(f);const int before=f.commits;
				if(mode==6)f.geometry.knife_held=true;
				if(mode==7)f.owner.support=vr::hand(1-int(rear));
				if(mode==8)f.trigger(true);
				if(mode==9)f.manipulation=false;
				if(mode>=10 && mode<=12){auto s=f.state;if(mode!=12){s.magazine_rounds=0;s.magazine_inserted=mode!=11;}else{s.action=m::action_state::closed;s.chamber_loaded=true;--s.magazine_rounds;}f.adopt(s);}
				if(mode==13)f.geometry.catch_input.valid=false;
				if(mode==15)++f.geometry.instance_generation;
				if(mode==16)f.writable=false;
				const auto total=m::total_rounds(f.state);const auto loaded=f.native.loaded;
				if(mode==1){for(int n=0;n<100;++n)point(f,.12f-n*.001f);}
				else if(mode==2){point(f,-.12f);point(f,-.065f);point(f,-.015f);}
				else if(mode==4){point(f,.12f);point(f,-.3f);}
				else if(mode==5){point(f,.015f);point(f,-.015f);}
				else {point(f,.12f,true,mode==17);if(mode==14)++f.input.reference_generation;point(f,.065f,mode!=3,mode==17);point(f,.015f,mode!=3,mode==17);}
				const bool accepted=mode==0 || mode==17;
				check(f.commits==before+int(accepted),"only a fresh free-hand left-to-receiver impact releases a loaded follower lock");
				check(m::total_rounds(f.state)==total && f.native.loaded==loaded,"receiver release moves one round internally without spending or creating ammo");
				if(accepted)check(m::ready(*f.rules,f.state) && f.state.magazine_rounds==f.rules->magazine_capacity-1 && f.last_effect==m::effect::action_close,"slap chambers once and uses existing bolt-close feedback");
				const auto commits=f.commits;f.step(false);f.step();check(f.commits==commits,"duplicate sample and sustained contact cannot repeat bolt release");
				if(mode==16){f.writable=true;point(f,.12f);point(f,.065f);point(f,.015f);check(m::ready(*f.rules,f.state),"failed native compare retries only after a separated new slap");}
			}
			Fixture empty(definition);auto s=empty.state;s.chamber_loaded=false;s.magazine_rounds=0;s.action=m::action_state::locked_open;
			for(bool inserted:{false,true}){s.magazine_inserted=inserted;const auto tx=m::plan(*empty.rules,s,{m::operation::release_catch,s.weapon,s.instance_generation,s.revision,rear,vr::hand(1-int(rear))});check(!tx,"dedicated receiver transaction cannot eject or release an absent/empty magazine");}
		}
	}
}
