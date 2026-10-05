#pragma once
#include "component/vr/gameplay/controller_firing.hpp"
#include "component/vr/gameplay/support_grip.hpp"
#include "component/vr/gameplay/hand_interaction/core.hpp"

template<class Check> void slow_simulation_input_tests(Check check)
{
	using namespace vr::controller_input;
	using namespace vr::gameplay::weapons;
	frame f{};const auto start=clock::now();f.sequence=f.reference_generation=f.continuity_generation=1;
	f.focused=true;f.sampled_at=start;
	for(int h=0;h<2;++h){f.grip[h].valid=f.aim[h].valid=true;f.trigger[h].active=f.squeeze[h].active=true;f.trigger[h].generation=f.squeeze[h].generation=1;}
	trigger_policy fire;hold owner{49,1,hand::right,hand::none,hold_source::engine_default,1};
	check(!fire.consume(f,owner,true,true,start),"slow-simulation firing first arms on neutral");
	vr::gameplay::hand_interaction::input_history gestures;gestures.update(f,true,start);
	support_grip support;support.consume(f,owner,1,true,.01f,.1f,.2f,start);
	for(int tick=1;tick<=60;++tick)
	{
		auto next=f;next.sequence++;next.sampled_at=start+std::chrono::milliseconds(tick*10);
		if(tick==10){next.trigger[1].down=next.squeeze[0].down=true;++next.trigger[1].presses;++next.squeeze[0].presses;}
		check(!producer_discontinuity(f,next),"continuous tracked samples do not depend on consumer frequency");f=next;
		if(tick%20==0)
		{
			check(fire.consume(f,owner,true,true,f.sampled_at),"fresh held trigger survives 200ms simulation cadence");
			check(support.consume(f,owner,1,true,.01f,.1f,.2f,f.sampled_at)==hand::left,"support hand acquires and retains its grip at slow cadence");
			gestures.update(f,true,f.sampled_at);
			const auto grip=gestures.get(hand::left,vr::gameplay::hand_interaction::button::grip);
			check(grip.armed && grip.down && (tick!=20 || grip.press),"slow server retains grip intent and its new-press edge");
		}
	}
	gestures.update(f,true,f.sampled_at+std::chrono::milliseconds(151));
	check(!gestures.get(hand::left,vr::gameplay::hand_interaction::button::grip).armed,"repeated sequence cannot retain a grip after tracking becomes stale");
	++f.continuity_generation;++f.sequence;f.sampled_at+=std::chrono::milliseconds(10);
	check(!fire.consume(f,owner,true,true,f.sampled_at),"producer/context discontinuity still requires trigger release");
	gestures.update(f,true,f.sampled_at);
	check(!gestures.get(hand::left,vr::gameplay::hand_interaction::button::grip).armed,"context epoch cancels old grip arming");
	f.trigger[1].down=false;fire.consume(f,owner,true,true,f.sampled_at);
	f.trigger[1].down=true;++f.trigger[1].presses;
	check(fire.consume(f,owner,true,true,f.sampled_at),"new press works after neutral rearming");
	check(!fire.consume(f,owner,true,true,f.sampled_at+std::chrono::milliseconds(151)),"real stale tracking is rejected even with a continuous generation");
	auto bad=f;bad.focused=false;check(producer_discontinuity(f,bad),"focus loss is recorded by producer");
	bad=f;bad.aim[0].valid=false;check(producer_discontinuity(f,bad),"lost hand tracking is recorded even between server polls");
	bad=f;bad.sampled_at+=std::chrono::milliseconds(151);check(producer_discontinuity(f,bad),"actual producer gap is recorded");
	consumer_continuity legacy;f.continuity_generation=0;
	legacy.update(f,start);check(legacy.update(f,start+std::chrono::milliseconds(200)),"unannotated synthetic/legacy inputs retain conservative gap handling");
}
