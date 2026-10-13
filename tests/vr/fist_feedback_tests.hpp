#pragma once
#include "component/vr/gameplay/fist_feedback.hpp"
#include <limits>

namespace fist_feedback_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr;using namespace std::chrono_literals;
		controller_input::frame input;input.sequence=12;input.reference_generation=3;input.continuity_generation=2;
		input.focused=true;input.sampled_at=controller_input::clock::now();input.grip[0].valid=input.aim[0].valid=true;
		input.squeeze[0]={true,true,1,4,0};
		gameplay::melee::fist_contact hit{hand::left,7,3,2,12,input.squeeze[0],input.sampled_at,{1,2,3}};
		const auto accepts=[&](auto frame,bool empty=true,std::uint64_t revision=7){
			return gameplay::melee::fresh(hit,frame,true,empty,revision,input.sampled_at+10ms);
		};
		check(accepts(input),"confirmed empty-hand strike needs no firearm lease");
		check(!accepts(input,false) && !accepts(input,true,8),"acquiring equipment or ending the free-hand interval rejects queued fist audio");
		auto next=input;next.squeeze[0].releases++;next.squeeze[0].presses++;
		check(!accepts(next),"release and reclose between simulation and playback cannot replay a punch");
		next=input;next.continuity_generation++;check(!accepts(next),"tracking discontinuity retires fist audio");
		next=input;next.reference_generation++;check(!accepts(next),"recenter or level reference change retires fist audio");
		next=input;next.focused=false;check(!accepts(next),"focus loss rejects fist audio");
		next=input;next.orientation_settling=true;check(!accepts(next),"calibration settling rejects fist audio");
		next=input;next.grip[0].valid=false;check(!accepts(next),"lost striking-hand tracking rejects fist audio");
		check(!gameplay::melee::fresh(hit,input,true,true,7,input.sampled_at+101ms) &&
			!gameplay::melee::fresh(hit,input,true,true,7,input.sampled_at-1ms),"expired or future contacts reject fist audio");
		check(!gameplay::melee::fresh(hit,input,false,true,7,input.sampled_at),"pause and scripted restrictions reject fist audio");
		hit.position[0]=std::numeric_limits<float>::quiet_NaN();check(!accepts(input),"invalid impact position cannot enter native playback");
	}
}
