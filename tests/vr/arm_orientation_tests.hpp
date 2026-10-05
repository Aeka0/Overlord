#pragma once
#include "component/vr/gameplay/hand_pose_solver.hpp"

namespace arm_orientation_tests
{
	using namespace vr::gameplay::hands;
	inline float angle(quat a,quat b)
	{
		a=normalize(a);b=normalize(b);float cosine{};
		for(int i=0;i<4;++i)cosine+=a[i]*b[i];
		return 2*std::acos(std::clamp(std::abs(cosine),0.f,1.f))*57.29577951f;
	}
	template<class Check> void run(Check check)
	{
		const quat identity{0,0,0,1},world=normalize(quat{.2f,.3f,-.1f,.7f});
		const auto close=[](vec a,vec b){return length(sub(a,b))<.0001f;};
		for(int segment=0;segment<2;++segment)for(float mirror:{1.f,-1.f})
		{
			const vec old_upper{3,mirror,-4},old_lower{3,-mirror,4};
			const auto opposite=scale(unit(segment ? old_lower : old_upper),-1);
			const auto tangent=unit(cross(opposite,{0,1,0})),other=cross(opposite,tangent);
			quat previous{},first{};float total{};
			for(int step=0;step<=360;++step)
			{
				const float t=float(step)*.01745329252f;
				// A tiny circle about the old segment's antipode previously caused
				// a complete axial spin, despite only two degrees of limb movement.
				const auto moving=unit(add(opposite,scale(add(scale(tangent,std::cos(t)),scale(other,std::sin(t))),.035f)));
				const auto fixed=unit(vec{.2f,mirror,-.3f});
				const auto upper=segment ? fixed : moving,lower=segment ? moving : fixed;
				const auto delta=orient_arm(old_upper,old_lower,upper,lower,identity);
				const auto rotation=segment ? delta.lower : delta.upper;
				check(close(rotate(delta.upper,unit(old_upper)),upper) && close(rotate(delta.lower,unit(old_lower)),lower),
					"both arm bones retain exact segment alignment around antiparallel directions");
				check(close(rotate(delta.upper,unit(cross(old_upper,old_lower))),unit(cross(upper,lower))) &&
					close(rotate(delta.lower,unit(cross(old_upper,old_lower))),unit(cross(upper,lower))),
					"upper arm and elbow share one oriented bend plane");
				if(step){const auto change=angle(previous,rotation);total+=change;check(change<.3f,"antipodal circle cannot flip either arm bone");}
				else first=rotation;
				previous=rotation;
				const auto rotated=orient_arm(rotate(world,old_upper),rotate(world,old_lower),rotate(world,upper),rotate(world,lower),world);
				check(angle(rotated.lower,multiply(multiply(world,delta.lower),conjugate(world)))<.1f,
					"arm roll convention is independent of world and head orientation");
				const auto repeated=orient_arm(upper,lower,upper,lower,multiply(delta.upper,identity));
				check(angle(repeated.upper,identity)<.1f && angle(repeated.lower,identity)<.1f,"repeated part-hand IK cannot accumulate arm roll");
			}
			check(total<40 && angle(first,previous)<.1f,"small hand circle cannot wind the upper arm or elbow through 360 degrees");
		}
		{
			// Read-only viewhands_us_army trace, 2026-09-29: a 3.47-degree
			// forearm-direction change caused 152.63 degrees of elbow rotation.
			const vec old_upper{5.72038984f,0.13722324f,-10.65460587f},old_lower{11.57084799f,-0.03285027f,0.06877136f};
			const std::array<vec,2> upper{{{3.99856377f,-6.97860336f,-9.03177261f},{4.47596788f,-6.88326168f,-8.87968063f}}},lower{{{-11.56431103f,0.26605511f,0.29372597f},{-11.55883074f,0.34947395f,-0.40208054f}}};
			const auto a=orient_arm(old_upper,old_lower,upper[0],lower[0],identity);
			const auto b=orient_arm(old_upper,old_lower,upper[1],lower[1],identity);
			check(angle(from_to(old_lower,lower[0]),from_to(old_lower,lower[1]))>150,
				"captured trace reproduces the old elbow-roll singularity");
			check(angle(a.lower,b.lower)<4 && angle(a.upper,b.upper)<4,
				"captured arm movement stays continuous using the bend plane");
		}
		for(const quat rotation:{identity,world})
		{
			const auto axis=rotate(rotation,{1,0,0});
			const auto straight=orient_arm(axis,axis,axis,axis,rotation);
			check(angle(straight.upper,identity)<.1f && angle(straight.lower,identity)<.1f,
				"straight native arm uses a finite anatomical frame without changing a matching pose");
		}
	}
}
