#pragma once
#include "component/vr/gameplay/fixed_sniper_hand_aim.hpp"
#include <limits>

template<class Check> void fixed_sniper_hand_aim_tests(Check check)
{
	using namespace vr;
	using namespace controller_input;
	using namespace gameplay::fixed_sniper;
	const auto close=[](float a,float b){return std::abs(a-b)<.00001f;};
	const float field=2*std::atan(.05f)*57.295779513f;
	struct fixture
	{
		hand_aim policy;frame input;hand_context context;clock::time_point now=clock::now();
		fixture(){input.focused=true;input.reference_generation=1;context.tracked=3;context.tan_half_y=.05f;context.noise=0;for(auto& b:input.squeeze){b.active=true;b.generation=1;}}
		hand_delta step(std::uint64_t epoch=1,bool allowed=true){now+=std::chrono::milliseconds(10);input.sampled_at=now;++input.sequence;return policy.consume(input,epoch,allowed,context,now);}
		void press(unsigned h){input.squeeze[h].down=true;++input.squeeze[h].presses;}
		void release(unsigned h){input.squeeze[h].down=false;++input.squeeze[h].releases;}
	};
	{
		fixture f;f.press(0);check(f.step().active==0,"held entry squeeze cannot acquire hand aiming");
		f.release(0);f.step();f.press(0);auto d=f.step();
		check(d.active==1 && d.pitch==0 && d.yaw==0,"deliberate left squeeze anchors without changing aim");
		f.context.positions[0][1]=-.02f;d=f.step();
		check(close(d.yaw,-field*.1f) && d.pitch==0,"two centimeters of rightward hand travel moves one tenth of the image height");
		d=f.policy.consume(f.input,1,true,f.context,f.now);
		check(d.active==1 && d.yaw==0 && d.pitch==0,"repeated command reads never replay the same physical displacement");
		f.context.positions[0][0]=.1f;d=f.step();check(d.yaw==0 && d.pitch==0,"forward hand motion cannot steer or zoom the scope");
		f.input.runtime_grip[0].tracking.orientation={{{0,1,0},{-1,0,0},{0,0,1}}};d=f.step();
		check(d.yaw==0 && d.pitch==0,"controller rotation does not participate in fixed-scope aiming");
		f.context.basis={{{0,1,0},{-1,0,0},{0,0,1}}};f.context.positions[0][1]-=.01f;d=f.step();
		check(close(d.yaw,-field*.05f) && d.pitch==0,"turning the head cannot rotate an active hand-drag coordinate frame");
		f.context.tan_half_y=.01f;d=f.step();check(d.pitch==0 && d.yaw==0,"changing magnification with a stationary hand never pans the image");
		f.context.positions[0][2]+=.01f;d=f.step();const auto zoom_field=2*std::atan(.01f)*57.295779513f;
		check(close(d.pitch,-zoom_field*.05f),"higher magnification automatically reduces angular hand gain to preserve image-space response");
		f.release(0);f.context.positions[0]={.4f,.5f,.6f};d=f.step();check(!d.active && !d.pitch && !d.yaw,"release freezes aim even while repositioning the hand");
		f.press(0);d=f.step();check(d.active==1 && !d.pitch && !d.yaw,"regripping establishes a new mouse-like origin without returning to old aim");
		f.context.positions[0][1]-=.01f;check(f.step().yaw<0,"the next drag continues from the new origin");
	}
	{
		fixture f;f.step();f.press(0);f.press(1);check(f.step().active==3,"both physical squeeze buttons can own translation control");
		f.context.positions[0][1]=f.context.positions[1][1]=-.01f;auto d=f.step();
		check(close(d.yaw,-field*.05f),"two equal hand motions average rather than doubling sensitivity");
		f.context.positions[0][1]-=.01f;d=f.step();check(close(d.yaw,-field*.025f),"the stationary held hand stabilizes the other hand by averaging");
		f.context.tracked=1;++f.input.continuity_generation;f.context.positions[0][1]-=.01f;d=f.step();
		check(d.active==1 && close(d.yaw,-field*.05f),"loss of the other controller cannot cancel a valid clutched hand");
		f.context.tracked=3;check(f.step().active==1,"a reconnected held squeeze needs its own release before rejoining");
		f.release(1);f.step();f.press(1);d=f.step();check(d.active==3 && d.yaw==0,"second hand can join without an aim jump");
		f.release(0);f.context.positions[1][2]=.01f;d=f.step();check(d.active==2 && close(d.pitch,-field*.05f),"right hand alone supports the same vertical drag");
	}
	{
		fixture f;f.context.noise=.00035f;f.step();f.press(0);f.step();
		f.context.positions[0][2]=.0002f;check(f.step().pitch==0,"sub-millimeter tremor stays inside spatial hysteresis");
		f.context.positions[0][2]=.00135f;auto d=f.step();
		check(close(d.pitch,-field*.001f/.2f),"deliberate fine movement responds immediately outside the noise radius");
		check(close(f.step().pitch,0),"stopping a hand has no temporal smoothing tail");
		f.context.positions[0][1]=1;d=f.step();check(!d.active && d.yaw==0,"tracking teleport is discarded instead of jerking the scope");
		check(!f.step().active,"tracking teleport requires a release and regrip");
	}
	for(unsigned mode=0;mode<7;++mode)
	{
		fixture f;f.step();f.press(0);f.step();f.context.positions[0][1]=-.02f;
		if(mode==0)f.step(1,false);
		if(mode==1){++f.input.reference_generation;f.step();}
		if(mode==2){f.step(2);}
		if(mode==3){f.now+=std::chrono::seconds(1);f.step();}
		if(mode==4){f.context.positions[0][1]=std::numeric_limits<float>::quiet_NaN();f.step();f.context.positions[0][1]=0;}
		if(mode==5){f.context.travel=.3f;f.step();}
		if(mode==6){f.context.tan_half_y=0;f.step();f.context.tan_half_y=.05f;}
		const auto d=f.step(mode==2?2:1);
		check(!d.active && !d.pitch && !d.yaw,"pause, recenter, ownership, stale pose, malformed pose, tuning and invalid FOV all rearm held clutches");
	}
}
