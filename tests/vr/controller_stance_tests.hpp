#pragma once
#include "component/vr/gameplay/controller_stance.hpp"
#include <limits>

template<class Check> void controller_stance_tests(Check check)
{
	using namespace vr::controller_input;using namespace std::chrono_literals;
	struct fixture
	{
		frame input;stance_gesture control;posture actual{posture::stand};bool gameplay{true},accept{true};int requests{};
		fixture(){input.sequence=1;input.reference_generation=1;input.sampled_at=clock::time_point{1s};input.focused=input.turn_active=true;sample(0,0);}
		stance_command sample(float x,float y,std::chrono::milliseconds dt=10ms,bool pressed=false,bool held=false)
		{
			++input.sequence;input.sampled_at+=dt;input.turn={x,y};
			auto result=control.consume(input,actual,gameplay,input.sampled_at,pressed,held);
			if(result.target){++requests;if(accept)actual=*result.target;}return result;
		}
	};
	for(bool down:{true,false})
	{
		fixture f;f.actual=down?posture::stand:posture::prone;const float y=down?-.9f:.9f;
		check(f.sample(.4f,y).target==posture::crouch,"90 percent vertical deflection immediately moves one posture level even on a diagonal");
		check(!f.control.consume(f.input,f.actual,true,f.input.sampled_at,false,false).target,"duplicate command consumption cannot turn immediate crouch into prone");
		for(int i=0;i<44;++i)check(!f.sample(0,y).target,"continuous hold cannot immediately skip crouch");
		check(f.sample(0,y).target==(down?posture::prone:posture::stand),"450 ms continuous hold progresses one more accepted native posture");
		for(int i=0;i<200;++i)check(!f.sample(0,y).target,"sustained limit posture never repeats or makes upward input jump");
		check(f.requests==2,"one uninterrupted stroke produces at most two stance requests");
	}
	{
		fixture f;f.sample(0,-.9f);
		f.sample(.8f,0);f.sample(.4f,-.9f);
		check(f.actual==posture::prone && f.requests==2,"two separate pushes descend without waiting for the hold repeat delay");
	}
	for(auto axis:{std::array<float,2>{0,-.899f},{.4f,-.899f},{1,0},{.7f,-.7f}})
	{
		fixture f;for(int i=0;i<200;++i)f.sample(axis[0],axis[1]);check(f.requests==0,"vertical deflection below 90 percent never changes posture regardless of horizontal input");
		check(f.sample(.4f,-.9f).target==posture::crouch,"turning may transition directly into a vertical stance request without an angle gate");
	}
	{
		fixture f;f.accept=false;for(int i=0;i<200;++i)f.sample(0,-1);
		check(f.requests==1 && f.actual==posture::stand,"native rejection cannot fabricate crouch or repeat into prone");
		f.actual=posture::crouch;f.accept=true;for(int i=0;i<100;++i)f.sample(0,-1);
		check(f.requests==1,"late native posture change does not revive an expired held request");
		f.sample(0,0);for(int i=0;i<11;++i)f.sample(0,-1);check(f.actual==posture::prone,"neutral and deliberate new push retries after a rejected transition");
	}
	for(int mode=0;mode<6;++mode)
	{
		fixture f;f.sample(0,-1);for(int i=0;i<5;++i)f.sample(0,-1);
		const auto before=f.requests;
		if(mode==0)f.gameplay=false;if(mode==1)f.input.focused=false;if(mode==2)++f.input.reference_generation;
		if(mode==3)f.input.turn_active=false;if(mode==4){++f.input.sequence;f.input.sampled_at+=10ms;f.input.turn[0]=std::numeric_limits<float>::quiet_NaN();}
		if(mode==4)f.control.consume(f.input,f.actual,true,f.input.sampled_at,false,false);
		else f.sample(0,-1,mode==5?200ms:10ms);
		f.gameplay=f.input.focused=f.input.turn_active=true;
		for(int i=0;i<100;++i)f.sample(0,-1);
		check(f.requests==before,"pause, focus, reference, inactive axis, invalid sample or gap requires neutral rearming");
	}
	for(auto actual:{posture::crouch,posture::prone})
	{
		fixture f;f.actual=actual;const auto click=f.sample(0,-1,10ms,true,true);
		check(click.target==posture::stand && click.suppress_jump,"stick click raises from crouch/prone without injecting an extra jump");
		for(int i=0;i<100;++i)check(f.sample(0,-1,10ms,false,true).suppress_jump,"holding rise click cannot jump once the native character reaches stand");
		f.sample(0,0);check(!f.sample(0,0,10ms,true,true).suppress_jump,"fresh click while standing retains ordinary jump");
	}
	{
		fixture f;f.input.turn_active=false;f.actual=posture::prone;
		check(f.sample(0,0,10ms,true,true).target==posture::stand,"valid jump click can rise even if the separate turn action is unavailable");
	}
	check(native_posture(1)==posture::prone && native_posture(2)==posture::crouch && native_posture(0)==posture::stand,"native movement flags decode observed stance, not requested stance");
	check(stance_binding(posture::crouch,0)==101 && stance_binding(posture::crouch,2)==101 && stance_binding(posture::prone,1)==100 &&
		stance_binding(posture::stand,1)==89 && stance_binding(posture::stand,2)==99 && stance_binding(posture::stand,0)==0,
		"native absolute down requests and observed-request-specific rise route never toggle a standing player down");
}
