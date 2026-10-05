#pragma once
#include "component/vr/gameplay/vehicle_steering.hpp"
#include "component/vr/gameplay/snowmobile_handle_pose.hpp"
#include <limits>

namespace vehicle_steering_tests
{
	namespace v=vr::gameplay::vehicles;
	using namespace vr::gameplay::hands;
	using namespace std::chrono_literals;
	inline vec rotated(vec p,vec axis,float degrees)
	{
		const float a=-degrees*vr::pose_filter::radians*.5f,s=std::sin(a);
		return rotate({axis[0]*s,axis[1]*s,axis[2]*s,std::cos(a)},p);
	}
	struct fixture
	{
		v::steering_geometry g;
		v::handle_steering control;
		std::array<vec,2> p;
		vr::controller_input::clock::time_point now{100s};
		fixture(v::steering_geometry geometry,unsigned mask):g(geometry),p(g.contacts)
		{
			control.update(p,3,now);
			for(unsigned h=0;h<2;++h)if(mask&(1u<<h))control.grab(h,p,g);
		}
		void step(unsigned valid=3){control.update(p,valid,now+=10ms);}
		void at_angle(float angle,vec translation={})
		{
			for(unsigned h=0;h<2;++h)p[h]=add(add(g.pivot,rotated(sub(g.contacts[h],g.pivot),g.axis,angle)),translation);
		}
		void settle(){for(unsigned i=0;i<40;++i)step();}
	};
	template<class Check>void run(Check check)
	{
		const auto native=v::handle_control_geometry({},v::snowmobile::wrists,39.37007874f);
		check(length(sub(native.contacts[0],native.contacts[1]))>.6f,"virtual control uses the captured native grip spacing");
		for(float units:{20.f,39.37007874f,80.f})for(float yaw:{0.f,71.f,179.f})
		{
			const auto tilt=from_to({0,0,1},unit({.3f,.1f,1.f}));
			const float a=yaw*vr::pose_filter::radians*.5f;
			const anchor handle{{100,200,-50},multiply({0,0,std::sin(a),std::cos(a)},tilt)};
			const auto geometry=v::handle_control_geometry(handle,v::snowmobile::wrists,units);
			for(float angle:{-18.f,-10.f,0.f,10.f,18.f})
			{
				fixture left(geometry,1),right(geometry,2),both(geometry,3);
				left.at_angle(angle);right.at_angle(angle);both.at_angle(angle);
				left.settle();right.settle();both.settle();
				check(std::abs(left.control.raw_angle()-angle)<.002f && std::abs(right.control.raw_angle()-angle)<.002f && std::abs(both.control.raw_angle()-angle)<.002f,
					"driver steering plane reconstructs physical turn independently of vehicle attitude and world scale");
				check(std::abs(left.control.value()-both.control.value())<.002f && std::abs(right.control.value()-both.control.value())<.002f,
					"equal handle angle has equal one/two-hand gain throughout the range");
			}
		}
		{
			fixture f(native,3);f.at_angle(10,{.5f,-.4f,.8f});f.settle();
			check(std::abs(f.control.raw_angle()-10)<.001f,"two-hand angle rejects whole-body translation in all axes");
			f.at_angle(0,{1,-2,3});f.settle();check(f.control.value()==0 && f.control.held()==3,"large common translation remains straight without releasing Grip");
		}
		for(unsigned mask:{1u,2u,3u})
		{
			fixture f(native,mask);
			for(unsigned i=0;i<200;++i){f.at_angle((i&1)?1.8f:-1.8f);f.step();check(f.control.value()==0,"centre jitter does not create a steering command");}
			f.at_angle(5);f.settle();check(f.control.value()>0 && f.control.value()<.025f,"gentle correction has a low-gain response outside centre");
			f.at_angle(30);for(unsigned i=0;i<15;++i)f.step();check(f.control.value()>.99f,"deliberate full turn reaches native full command within 150 ms");
		}
		for(unsigned first:{0u,1u})
		{
			fixture f(native,3);f.control.release(1u<<(1-first));f.step();f.at_angle(10);f.settle();const float before=f.control.value();
			f.control.grab(1-first,f.p,f.g);f.step();check(std::abs(f.control.value()-before)<.001f,"rejoining the calibrated pair does not change steering angle or gain");
			f.at_angle(14);f.settle();check(std::abs(f.control.raw_angle()-14)<.002f,"second grip preserves the existing angular reference");
			const float turn=f.control.value();f.control.release(1u<<first);f.step();
			check(std::abs(f.control.value()-turn)<.001f,"dropping either hand preserves the turn for the remaining hand");
			f.at_angle(0);f.settle();check(std::abs(f.control.raw_angle())<.002f && f.control.value()==0,"remaining hand returns to the same straight-ahead pose");
			f.control.release(3);check(!f.control.held() && !f.control.value(),"last Grip release immediately removes physical steering");
		}
		{
			fixture f(native,3);f.at_angle(-12);f.settle();f.step(1);
			check(f.control.held()==1 && f.control.value()<0,"tracking loss retires only the unavailable hand");
			f.control.update(f.p,3,f.now+151ms);check(!f.control.held() && !f.control.value(),"stale tracking retires calibration");
		}
		for(unsigned remain:{0u,1u})
		{
			fixture f(native,0);const vec shift{.03f,-.04f,.02f};
			f.p[0]=add(f.p[0],{0,.08f,0});f.p[1]=add(f.p[1],{0,-.04f,.03f});
			for(auto& p:f.p)p=add(p,shift);
			f.control.grab(0,f.p,f.g);f.control.grab(1,f.p,f.g);
			const auto pivot=f.control.pivot();const auto initial=f.p;
			for(unsigned h=0;h<2;++h)f.p[h]=add(pivot,rotated(sub(initial[h],pivot),f.g.axis,12));
			f.settle();f.control.release(1u<<(1-remain));f.step();
			for(unsigned h=0;h<2;++h)f.p[h]=add(pivot,rotated(sub(initial[h],pivot),f.g.axis,17));
			f.settle();check(std::abs(f.control.raw_angle()-17)<.002f,"unequal physical grasp offsets keep their lever radius after one hand releases");
		}
		{
			fixture f(native,3);f.p[1]=f.p[0];f.step();
			check(f.control.held()==3 && !f.control.sample_valid() && f.control.value()==0,"coincident hands suppress the undefined angle without detaching");
			f.at_angle(0);f.step();f.at_angle(10);f.settle();check(f.control.value()>0,"separated hands recover a stable reference");
			f.p[0][0]=std::numeric_limits<float>::quiet_NaN();f.step();
			check(f.control.held()==2 && std::isfinite(f.control.value()),"nonfinite tracking cannot poison the remaining hand");
		}
		{
			fixture f(native,1);const auto ray=sub(f.p[0],f.control.pivot());f.p[0]=add(f.control.pivot(),scale(ray,5));f.settle();
			check(f.control.held()==1 && f.control.value()==0,"single-hand radial movement neither turns nor causes breakaway");
			f.p[0]=add(f.control.pivot(),rotated(scale(ray,5),f.g.axis,30));f.settle();
			check(f.control.held()==1 && f.control.value()>.99f,"distant valid grip still controls its angle");
		}
		{
			fixture f(native,1);f.p[0]=add(f.p[0],{0,-.08f,.10f});f.settle();
			check(std::abs(f.control.raw_angle())<.001f && !f.control.value(),"inward and upward arm relaxation does not turn the single-hand control");
		}
		{
			const auto g=v::handle_control_geometry({},v::snowmobile::wrists,39.37007874f,unit({.2f,.1f,1}));
			fixture f(g,3);f.p[0]=add(f.p[0],scale(g.axis,.12f));f.p[1]=add(f.p[1],scale(g.axis,-.10f));f.settle();
			check(!f.control.value(),"physical vertical motion stays out of steering after a tilted tracking reference");
			f.at_angle(10);f.settle();check(std::abs(f.control.raw_angle()-10)<.001f,"tracking-up plane retains the correct turn angle");
		}
		{
			// Captured nearly level hands inherited -11.4 degrees and -48 percent
			// output from the old single-hand stem-pivot baseline.
			fixture f(native,0);f.p={vec{.325239f,.320377f,-.486023f},vec{.322659f,-.284658f,-.478805f}};
			f.control.grab(0,f.p,f.g);f.p={vec{.288925f,.247352f,-.484783f},vec{.302022f,-.325040f,-.501377f}};f.settle();
			f.control.grab(1,f.p,f.g);f.settle();
			check(f.control.held()==3 && std::abs(f.control.raw_angle())<2 && !f.control.value(),"captured straight two-hand pose clears inherited single-hand bias");
		}
		{
			vr::controller_input::frame input;input.focused=true;
			for(unsigned h=0;h<2;++h){input.grip[h].valid=input.aim[h].valid=true;input.squeeze[h].active=input.squeeze[h].down=input.trigger[h].active=true;}
			input.trigger[1].down=true;
			check(v::physical_driver_input(1,.3f,input).throttle==0,"gun/free-hand trigger cannot accelerate a grip owned by the other hand");
			check(v::physical_driver_input(2,.3f,input).throttle==1 && v::physical_driver_input(2,.3f,input).steering==.3f,"holding-hand trigger supplies throttle alongside steering");
			input.trigger[0].down=true;check(v::physical_driver_input(3,0,input).throttle==1,"both handle triggers do not double throttle");
			input.squeeze[0].down=input.squeeze[1].down=false;check(v::physical_driver_input(3,1,input).throttle==0,"Grip release stops throttle before server ownership update");
			input.squeeze[0].down=true;input.grip[0].valid=false;check(v::physical_driver_input(1,1,input).throttle==0,"lost tracking cannot hold throttle");
			check(v::merge_driver_throttle(-.5f,1)==-.5f && v::merge_driver_throttle(.3f,1)==1,"stick brake/reverse overrides trigger throttle");
		}
		for(float initial:{-25.f,-12.f,17.f,179.f})
		{
			fixture f(native,0);f.at_angle(initial);f.control.grab(0,f.p,f.g);f.control.grab(1,f.p,f.g);f.settle();
			check(f.control.value()==0 && std::abs(f.control.raw_angle())<.001f,"comfortable bilateral grasp is neutral independently of room heading");
			f.at_angle(initial+10);f.settle();check(f.control.value()>.10f && f.control.value()<.12f,"ten degrees is a controlled correction instead of a sharp turn");
			f.control.center(f.p);f.settle();check(!f.control.value(),"handle B/Y recenter clears physical steering without releasing Grip");
			f.at_angle(initial+20);f.settle();check(std::abs(f.control.raw_angle()-10)<.002f,"manual neutral establishes a new consistent angle reference");
			f.control.release(3);f.control.grab(0,f.p,f.g);f.control.grab(1,f.p,f.g);f.settle();
			check(!f.control.value(),"regrasp after full release never inherits a prior neutral heading");
		}
	}
}
