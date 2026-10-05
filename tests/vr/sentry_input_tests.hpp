#pragma once
#include "component/vr/gameplay/sentry_input.hpp"

template<class Check>void sentry_input_tests(Check check)
{
	using namespace vr::controller_input;
	using vr::gameplay::sentry::placement_input;
	const auto start=clock::now();
	const auto ready=[&]
	{
		frame f;f.sequence=f.reference_generation=f.continuity_generation=1;f.sampled_at=start;f.focused=true;
		for(unsigned h=0;h<2;++h)
		{f.grip[h].valid=f.aim[h].valid=true;f.trigger[h].active=true;f.trigger[h].generation=1;}
		return f;
	};
	for(unsigned hand=0;hand<2;++hand)
	{
		auto f=ready();placement_input input;
		check(!input.consume(f,1,true,start),"sentry arms on neutral without a firearm owner");
		++f.trigger[hand].presses; // A complete short tap between command reads.
		check(input.consume(f,1,true,start),"either sentry trigger delivers a short tap");
		check(input.consume(f,1,true,start+std::chrono::milliseconds(99)),"sentry tap spans native 50ms polls");
		check(!input.consume(f,1,true,start+std::chrono::milliseconds(100)),"sentry tap expires without replay");
		f.trigger[hand].down=true;++f.trigger[hand].presses;
		f.sampled_at=start+std::chrono::milliseconds(110);++f.sequence;
		check(input.consume(f,1,true,f.sampled_at),"sentry accepts a new press");
		f.sampled_at=start+std::chrono::milliseconds(250);++f.sequence;
		check(input.consume(f,1,true,f.sampled_at),"held sentry trigger keeps native invalid-placement retry waiting");
		f.trigger[hand].down=false;
		check(!input.consume(f,1,true,f.sampled_at),"sentry release lets native retry rearm");
		++f.trigger[hand].presses;
		check(input.consume(f,1,true,f.sampled_at),"sentry placement can be retried after release");
		check(!input.consume(f,0,true,f.sampled_at),"ending native carry cancels pending placement");
		check(!input.consume(f,2,true,f.sampled_at),"new sentry session never replays an old tap");
	}
	{
		auto f=ready();placement_input input;
		for(auto& trigger:f.trigger){trigger.down=true;++trigger.presses;}
		check(!input.consume(f,1,true,start),"triggers held before pickup cannot immediately place sentry");
		for(auto& trigger:f.trigger)trigger.down=false;
		check(!input.consume(f,1,true,start),"release arms both sentry triggers");
		for(auto& trigger:f.trigger){trigger.down=true;++trigger.presses;}
		check(input.consume(f,1,true,start),"simultaneous sentry triggers merge into one attack request");
		f.trigger[0].down=false;f.sampled_at=start+std::chrono::milliseconds(110);
		check(input.consume(f,1,true,f.sampled_at),"one held trigger still blocks native retry when the other releases");
		f.trigger[1].down=false;
		check(!input.consume(f,1,true,f.sampled_at),"native retry sees release after both triggers release");
	}
	for(unsigned fault=0;fault<9;++fault)
	{
		auto f=ready();placement_input input;input.consume(f,1,true,start);
		f.trigger[0].down=true;++f.trigger[0].presses;
		check(input.consume(f,1,true,start),"sentry setup accepts trigger before interruption");
		auto bad=f;auto at=start;auto epoch=std::uint64_t{1};bool allowed=true;
		switch(fault)
		{
		case 0:allowed=false;break;
		case 1:bad.focused=false;break;
		case 2:bad.grip[0].valid=false;break;
		case 3:bad.trigger[0].active=false;break;
		case 4:++bad.reference_generation;break;
		case 5:++bad.continuity_generation;break;
		case 6:at+=std::chrono::milliseconds(151);break;
		case 7:bad.orientation_settling=true;break;
		case 8:epoch=2;break;
		}
		check(!input.consume(bad,epoch,allowed,at),"sentry interruption cancels held and buffered placement");
		check(!input.consume(f,1,true,start),"sentry interruption recovery requires trigger release");
		f.trigger[0].down=false;input.consume(f,1,true,start);
		f.trigger[0].down=true;++f.trigger[0].presses;
		check(input.consume(f,1,true,start),"sentry resumes after neutral and a new press");
	}
}
