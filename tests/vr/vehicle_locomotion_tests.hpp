#pragma once
#include "component/vr/gameplay/controller_locomotion.hpp"
#include <limits>

template<class Check> void vehicle_locomotion_tests(Check&& check)
{
	using namespace vr::controller_input;using namespace std::chrono_literals;
	vehicle_locomotion drive;frame input{};
	input.focused=input.move_active=input.turn_active=true;input.sequence=input.reference_generation=input.continuity_generation=1;
	input.sampled_at=clock::now();
	const auto sample=[&](bool gameplay=true){++input.sequence;input.sampled_at+=20ms;return drive.consume(input,gameplay,.2f,input.sampled_at);};
	const auto near=[](float a,float b){return std::abs(a-b)<.001f;};
	check(!sample().active,"vehicle requires a neutral baseline");
	input.move={0,1};auto out=sample();check(out.active && near(out.forward,1) && near(out.right,0),"left stick drives forward");
	input.move={};input.turn={0,-1};out=sample();check(near(out.forward,-1) && !out.yaw_delta,"right stick brakes/reverses without view yaw");
	input.turn={1,0};out=sample();check(near(out.right,1) && !out.yaw_delta && !out.discontinuous_turn,"right stick steers without snap turning");
	input.move={1,0};out=sample();check(near(out.right,1),"same-direction full sticks saturate at one");
	input.move={-1,0};out=sample();check(near(out.right,0) && near(out.forward,0),"opposing sticks cancel");
	input.move={0,1};out=sample();check(near(out.right,.70710678f) && near(out.forward,.70710678f) && std::hypot(out.right,out.forward)<=1.000001f,"orthogonal sticks sum then cap vector length");
	input.move={-.3f,0};input.turn={.7f,0};out=sample();check(near(out.right,.25f),"partial opposing vectors combine before shared radial deadzone");
	input.move={.15f,0};input.turn={-.1f,0};out=sample();check(near(out.right,0),"small resultant stays inside deadzone");
	input.move={0,1};input.turn={0,-1};check(!sample(false).active && !sample().active,"cancelled held sticks cannot arm after pause");
	input.turn={};check(!sample().active,"releasing one opposing stick after pause cannot accelerate");
	input.move={};(void)sample();input.turn={0,1};check(near(sample().forward,1),"both neutral rearm driving");
	++input.reference_generation;check(!sample().active,"recenter requires physical neutral");
	input.turn={};(void)sample();input.turn={1,0};check(near(sample().right,1),"recenter neutral restores right-hand steering");
	++input.continuity_generation;check(!sample().active,"tracking discontinuity cannot reuse steering latch");
	input.turn={};(void)sample();input.move_active=false;input.move={NAN,NAN};(void)sample();
	input.turn={.6f,.8f};out=sample();check(near(out.right,.6f) && near(out.forward,.8f),"right controller works when left axis is inactive");
	input.move_active=true;input.move={0,1};check(!sample().active,"reconnected deflected controller must rearm");
	input.move=input.turn={};(void)sample();input.turn_active=false;(void)sample();input.move={-1,0};check(near(sample().right,-1),"left controller works when right axis is inactive");
	input.move[0]=std::numeric_limits<float>::infinity();check(!sample().active,"nonfinite active axis rejected");
	input.move={2,0};check(!sample().active,"out-of-range active axis rejected");
	input.move={};(void)sample();input.move={0,1};
	check(!drive.consume(input,true,.2f,input.sampled_at+151ms).active,"stale driving snapshot rejected");
	check(!sample().active,"stale recovery with deflection requires neutral");
	input.move={};(void)sample();input.orientation_settling=true;check(!sample().active,"orientation reconfiguration blocks driver input");
	input.orientation_settling=false;(void)sample();
	// A normal boarding transition carries the current physical deflection.
	vehicle_locomotion boarding;input.move={0,1};input.turn={};input.move_active=input.turn_active=true;
	out=boarding.consume(input,true,.2f,input.sampled_at,true);
	check(out.active && near(out.forward,1),"held forward crosses boarding without another stick edge");
	(void)boarding.consume(input,false,.2f,input.sampled_at);
	check(!boarding.consume(input,true,.2f,input.sampled_at).active,"entry exception does not rearm a paused held stick");
	input.move={};(void)boarding.consume(input,true,.2f,input.sampled_at);input.turn={1,0};
	check(near(boarding.consume(input,true,.2f,input.sampled_at).right,1),"physical neutral still recovers after pause");
	// Walking's existing two channels remain separate.
	locomotion walk;input.turn_active=true;input.move=input.turn={};(void)walk.consume(input,true,.2f,{turn_mode::snap},input.sampled_at);
	input.turn={1,0};out=walk.consume(input,true,.2f,{turn_mode::snap},input.sampled_at);
	check(!out.forward && !out.right && out.yaw_delta==-30,"on-foot right stick still turns instead of driving");
}
