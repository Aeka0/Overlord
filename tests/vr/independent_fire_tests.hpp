#pragma once
#include "component/vr/gameplay/independent_fire_clock.hpp"
#include "component/vr/gameplay/weapon_instance_cache.hpp"
#include <limits>
#include "component/vr/gameplay/tube_feed.hpp"

namespace independent_fire_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;
		using namespace independent_fire;
		using namespace std::chrono_literals;
		vr::controller_input::frame input{};input.focused=true;input.reference_generation=1;
		for (int h=0;h<2;++h) {input.grip[h].valid=true;input.aim[h].valid=true;input.trigger[h].active=true;}
		auto now=vr::controller_input::clock::time_point{}+1s;
		const auto sample=[&](int ms,bool down) {
			now=vr::controller_input::clock::time_point{}+std::chrono::milliseconds(ms);input.sampled_at=now;++input.sequence;
			for (auto& button:input.trigger)
			{if (down && !button.down) ++button.presses;if (!down && button.down) ++button.releases;button.down=down;}
		};
		hold l86{14,1,hand::right,hand::none,hold_source::interaction,1};
		hold mg4{15,2,hand::left,hand::none,hold_source::interaction,2};
		shot_clock a,b;timing fast{0,75,0,0},slow{0,100,0,0};
		sample(975,false);a.due(input,l86,true,true,now,975,fast);b.due(input,mg4,true,true,now,975,slow);
		int ac{},bc{};
		for (int time=1000;time<=1300;time+=25)
		{
			sample(time,true);
			const bool x=a.due(input,l86,true,true,now,time,fast),y=b.due(input,mg4,true,true,now,time,slow);
			if (time==1000) check(x && y,"different weapons admit shots at the same simulation time");
			if (x) {++ac;a.settled(time,fast,true);}
			if (y) {++bc;b.settled(time,slow,true);}
			check(!a.due(input,l86,true,true,now,time,fast) && !b.due(input,mg4,true,true,now,time,slow),"duplicate callback cannot emit twice");
		}
		check(ac==5 && bc==4,"L86 and MG4 retain separate 75/100 ms cadence");
		sample(2000,true);check(!a.due(input,l86,true,true,now,2000,fast),"stall cancels held trigger without catch-up fire");
		sample(2025,false);a.due(input,l86,true,true,now,2025,fast);
		sample(2050,true);check(a.due(input,l86,true,true,now,2050,fast),"neutral then press resumes after stall");a.settled(2050,fast,true);
		a.cancel();sample(2075,false);a.due(input,l86,true,true,now,2075,fast);
		sample(2100,true);check(!a.due(input,l86,true,true,now,2100,fast),"cancel or stow does not clear committed cooldown");
		shot_clock semi;timing pistol{1,100,0,0};
		{
			timing ranger;
			check(decode_native_timing(6,10,0,0,ranger),"live H2 Ranger double-barrel mode 6 / 10 ms timing admitted");
			check(!valid({6,0,0,0}) && !valid({0,10,0,0}) && !valid({7,10,0,0}),"double-barrel timing exception does not admit invalid or unsafe automatic timing");
			shot_clock double_barrel;
			sample(2150,false);double_barrel.due(input,mg4,true,true,now,2150,ranger);
			sample(2160,true);check(double_barrel.due(input,mg4,true,true,now,2160,ranger),"first Ranger trigger edge fires one barrel");double_barrel.settled(2160,ranger,true);
			sample(2170,true);check(!double_barrel.due(input,mg4,true,true,now,2170,ranger),"holding Ranger Trigger never fires the second barrel automatically");
			sample(2180,false);double_barrel.due(input,mg4,true,true,now,2180,ranger);
			sample(2190,true);check(double_barrel.due(input,mg4,true,true,now,2190,ranger),"fresh Ranger edge can fire the other barrel at native cadence");
		}
		sample(2200,false);semi.due(input,mg4,true,true,now,2200,pistol);
		sample(2225,true);check(semi.due(input,mg4,true,true,now,2225,pistol),"semi-auto fires once on fresh press");semi.settled(2225,pistol,true);
		for (int time=2250;time<=2400;time+=25) {sample(time,true);check(!semi.due(input,mg4,true,true,now,time,pistol),"semi-auto hold never becomes automatic");}
		shot_clock burst;timing triple{3,50,0,100};
		sample(2500,false);burst.due(input,mg4,true,true,now,2500,triple);
		sample(2525,true);check(burst.due(input,mg4,true,true,now,2525,triple),"burst first shot");burst.settled(2525,triple,true);
		sample(2575,false);check(burst.due(input,mg4,true,true,now,2575,triple),"burst continues on release");burst.settled(2575,triple,true);
		sample(2625,false);check(burst.due(input,mg4,true,true,now,2625,triple),"burst last shot");burst.settled(2625,triple,true);
		sample(2650,true);check(!burst.due(input,mg4,true,true,now,2650,triple),"burst cooldown retained");
		++mg4.rear_revision;sample(2675,true);check(!burst.due(input,mg4,true,true,now,2675,triple),"handover cancels pending input and rearms");
		check(!valid({0,0,0,0}) && !valid({7,100,0,0}) && !valid({0,100,-1,0}),"invalid native cadence rejected");
		// Captured H2 descriptors: M16 70 ms, FAMAS 65 ms, M93R 60 ms.
		// Exercise the native-unit boundary too, rather than only synthetic clocks.
		for (int interval:{70,65,60})
		{
			timing native;
			check(decode_native_timing(3,interval,0,200.f,native) && native.burst_pause==200,
				"native 200 ms burst cooldown is admitted without seconds conversion");
			shot_clock left,right;
			sample(4000,false);left.due(input,mg4,true,true,now,4000,native);right.due(input,l86,true,true,now,4000,native);
			int left_shots{},right_shots{};
			for (int time=4005;time<=4500;time+=5)
			{
				sample(time,true);
				if (left.due(input,mg4,true,true,now,time,native)) {++left_shots;left.settled(time,native,true);}
				if (right.due(input,l86,true,true,now,time,native)) {++right_shots;right.settled(time,native,true);}
				if (time==4005) check(left_shots==1 && right_shots==1,"native burst weapons fire concurrently on first press");
			}
			check(left_shots==3 && right_shots==3,"each native burst stops at three shots while trigger stays held");
			sample(4505,false);left.due(input,mg4,true,true,now,4505,native);
			sample(4510,true);check(left.due(input,mg4,true,true,now,4510,native),"native burst rearms after release and cooldown");
			left.settled(4510,native,false);
			sample(4510+interval,true);check(!left.due(input,mg4,true,true,now,4510+interval,native),"empty or rejected shot cancels the rest of a burst");
		}
		timing decoded;
		for (float pause:{-1.f,10001.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
			check(!decode_native_timing(3,70,0,pause,decoded),"invalid native burst pause rejected before integer conversion");
		check(decode_native_timing(3,70,0,200.5f,decoded) && decoded.burst_pause==201,"native fractional pause rounds to milliseconds");
		check(decode_native_timing(1,70,0,200.f,decoded) && decoded.burst_pause==0,"semi-auto does not inherit the descriptor burst pause");
		instance_cache<shot_clock,2> clocks;
		auto right=l86,left=mg4;left.weapon=right.weapon;
		right.instance_generation=301;left.instance_generation=302;
		auto& first=*clocks.acquire(right.id());auto& second=*clocks.acquire(left.id());
		sample(6000,false);first.due(input,right,true,true,now,6000,fast);second.due(input,left,true,true,now,6000,fast);
		sample(6025,true);
		check(first.due(input,right,true,true,now,6025,fast) && second.due(input,left,true,true,now,6025,fast),"same-definition instances admit two shots in the same tick");
		first.settled(6025,fast,true);second.settled(6025,fast,true);
		first.cancel();sample(6100,true);
		check(!first.due(input,right,true,true,now,6100,fast) && second.due(input,left,true,true,now,6100,fast),"stowing or losing one identical gun does not cancel the other clock");
		second.settled(6100,fast,true);
		++left.instance_generation;sample(6175,true);
		check(!second.due(input,left,true,true,now,6175,fast),"same-model replacement cannot inherit held trigger intent even with matching grip revision");
		for(auto rear:{hand::left,hand::right})
		{
			hold lever{40,1,rear,hand::none,hold_source::interaction,1};timing normal{1,430,0,0};shot_clock rejected;
			sample(10000,false);rejected.due(input,lever,true,true,now,10000,normal);
			sample(10010,true);check(rejected.due(input,lever,true,true,now,10010,normal),"recorded early firing attempt reaches mechanical admission");rejected.rejected(10010);
			check(!rejected.due(input,lever,true,true,now,10010,normal),"rejected input cannot replay in the same command");
			for(int time=10050;time<=10150;time+=50){sample(time,false);rejected.due(input,lever,true,true,now,time,normal);}
			sample(10226,true);check(rejected.due(input,lever,true,true,now,10226,normal),"live T+216 ms retry is no longer blocked by a fabricated 430 ms rejection cooldown");rejected.settled(10226,normal,true);
			rejected.rejected(10230);sample(10250,false);rejected.due(input,lever,true,true,now,10250,normal);sample(10260,true);
			check(!rejected.due(input,lever,true,true,now,10260,normal),"rejecting another request never clears an ordinary weapon's real shot cooldown");
			const auto mechanical=mechanical_cycle_timing();
			sample(10280,false);rejected.due(input,lever,true,true,now,10280,mechanical);sample(10290,true);
			check(rejected.due(input,lever,true,true,now,10290,mechanical),"manual admission cannot inherit an older native firing deadline");
			check(valid(mechanical) && !valid({0,0,0,0,true}) && !valid({1,0,1,0,true}),"zero-interval policy only permits a manual single shot per fresh edge without delay or bursts");
			shot_clock clock;const tube::rules rules{5,tube::action_drive::lever,tube::feed_layout::tube,false};auto state=tube::import_native(rules,40,1,{5,20});
			sample(11000,false);clock.due(input,lever,true,true,now,11000,mechanical);
			for(int cycle=0;cycle<3;++cycle)
			{
				const int time=11010+cycle*30;sample(time,true);check(clock.due(input,lever,true,true,now,time,mechanical),"completed manual cycles may fire faster than the original 430 ms interval");
				const auto shot=tube::plan(rules,state,{tube::operation::accepted_shot,40,1,state.revision,rear,rear});check(bool(shot),"loaded closed chamber admits one manual shot");state=shot.next;clock.settled(time,mechanical,true);
				check(!tube::plan(rules,state,{tube::operation::accepted_shot,40,1,state.revision,rear,rear}),"removing cadence cannot fire again without cycling the mechanism");
				check(!clock.due(input,lever,true,true,now,time,mechanical),"zero interval still rejects duplicate same-command fire");
				const auto open=tube::plan(rules,state,{tube::operation::rack_open,40,1,state.revision,rear,rear});state=open.next;
				const auto close=tube::plan(rules,state,{tube::operation::rack_close,40,1,state.revision,rear,rear});state=close.next;
				sample(time+10,false);clock.due(input,lever,true,true,now,time+10,mechanical);
			}
			check(tube::native_ammo(state).loaded==2,"three complete manual cycles debit exactly three rounds");
		}

	}
}
