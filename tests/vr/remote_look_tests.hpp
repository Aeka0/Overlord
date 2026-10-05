#pragma once
#include "component/vr/remote_look.hpp"
#include "component/vr/digital_button_gate.hpp"
#include <limits>

template<class Check>void remote_look_tests(Check check)
{
	using namespace vr::controller_input;using namespace std::chrono_literals;
	{
		paired_button_gate fire;
		std::array<digital_action,2> triggers{{{true,true,1,1,0},{true,false,0,1,0}}};
		check(!fire.consume(triggers).held,"a held entry trigger cannot launch the UAV");
		triggers[1].down=true;++triggers[1].presses;auto result=fire.consume(triggers);
		check(result.held && result.pressed,"the other hand can fire independently of a held unarmed entry trigger");
		check(fire.consume(triggers).held && !fire.consume(triggers).pressed,"holding a trigger keeps boost without repeated launch notifications");
		triggers[1].down=false;triggers[0].down=false;(void)fire.consume(triggers);
		triggers[0].down=true;++triggers[0].presses;result=fire.consume(triggers);
		check(result.held && result.pressed,"the first hand can fire after its own neutral rearm");
		triggers[1].down=true;++triggers[1].presses;result=fire.consume(triggers);
		check(result.held && result.pressed,"both hand presses coalesce to one logical native attack notification");
		triggers[0].down=false;check(fire.consume(triggers).held,"releasing one hand does not cancel the other hand's boost");
		fire={};check(!fire.consume(triggers).held,"session changes rearm both trigger gates independently");
	}
	remote_look controls;frame input{};const auto start=clock::now();
	input.focused=input.turn_active=true;input.sequence=input.reference_generation=input.continuity_generation=1;input.sampled_at=start;
	input.turn={1,1};
	check(controls.consume(input,1,false,true,.2f,start).pitch==0,"held stick at remote entry cannot steer before neutral");
	input.turn={};input.sampled_at+=10ms;++input.sequence;(void)controls.consume(input,1,false,true,.2f,input.sampled_at);
	input.turn={1,1};input.sampled_at+=10ms;++input.sequence;const auto diagonal=controls.consume(input,1,false,true,.2f,input.sampled_at);
	check(diagonal.pitch>.7f && diagonal.yaw>.7f && diagonal.seconds>0 &&
		std::abs(std::hypot(diagonal.pitch,diagonal.yaw)-1)<.0001f,"remote right stick supplies both axes with radial normalization and no left-stick dependency");
	check(controls.consume(input,1,true,true,.2f,input.sampled_at).yaw==0,"switching to native missile axes rearms a held stick");
	input.turn={};input.sampled_at+=10ms;++input.sequence;(void)controls.consume(input,1,true,true,.2f,input.sampled_at);
	input.turn={0,-1};input.sampled_at+=10ms;++input.sequence;
	check(controls.consume(input,1,true,true,.2f,input.sampled_at).pitch==-1,"backward stick supplies native down-look input");
	input.focused=false;check(controls.consume(input,1,true,true,.2f,input.sampled_at).pitch==0,"focus loss cancels remote stick input");
	input.focused=true;check(controls.consume(input,1,true,true,.2f,input.sampled_at).pitch==0,"focus recovery cannot resume a held remote stick");
	input.turn={};input.sampled_at+=10ms;++input.sequence;(void)controls.consume(input,1,true,true,.2f,input.sampled_at);
	input.turn={.1f,.1f};input.sampled_at+=10ms;++input.sequence;
	const auto small=controls.consume(input,1,true,true,.2f,input.sampled_at);
	check(small.pitch==0 && small.yaw==0,"remote look respects the existing radial stick deadzone");
	input.turn={std::numeric_limits<float>::quiet_NaN(),0};
	check(controls.consume(input,1,true,true,.2f,input.sampled_at).yaw==0,"nonfinite remote stick input is rejected");
}
