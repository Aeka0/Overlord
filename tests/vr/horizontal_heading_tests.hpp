#pragma once
#include "component/vr/continuous_view_angles.hpp"
#include "component/vr/body_pose.hpp"
#include <limits>

namespace horizontal_heading_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::game_view;
		const auto axis = [](float pitch, float yaw, float roll) {
			constexpr float radians = .017453292519943295f;
			pitch *= radians; yaw *= radians; roll *= radians;
			const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw),cr=std::cos(roll),sr=std::sin(roll);
			return std::array<std::array<float,3>,3>{{{cp*cy,cp*sy,sp},
				{-cy*sp*sr-sy*cr,-sy*sp*sr+cy*cr,cp*sr},{-cy*sp*cr+sy*sr,-sy*sp*cr-cy*sr,cp*cr}}};
		};
		const auto close=[](float a,float b){return std::abs(std::remainder(a-b,360.f))<.002f;};
		for(float yaw : {-170.f,0.f,23.f,170.f})
		{
			horizontal_heading h;
			for(int pitch=-170;pitch<=170;++pitch)
				check(h.update(axis(float(pitch),yaw,0)) && close(h.yaw,yaw),
					"physical heading retains yaw across both pitch poles without Euler branch dependence");
			const auto before=h.yaw;
			check(h.update(axis(180,yaw,0)) && h.yaw==before,"inverted singularity retains the last defined heading");
			check(h.update(axis(12,yaw+40,3)) && std::abs(std::remainder(h.yaw-yaw-40,360.f))<1,
				"upright recovery immediately acquires current physical heading");
		}
		// Captured HMD-resume orientation. Euler continuity retained pitch ~162,
		// yaw ~72, roll ~-177 although this rotation is physically upright.
		const std::array<std::array<float,3>,3> captured{{
			{-.290007889f,-.905361950f,.310185939f},
			{.955855966f,-.290029168f,.047147274f},
			{.047277614f,.310166150f,.949506104f}}};
		continuous_angles old{161.929565f,72.238564f,-177.157333f};horizontal_heading heading;
		vr::body_pose::estimator body;
		for(int i=0;i<1000;++i)
		{
			check(old.update(captured) && heading.update(captured),"captured orientation accepted");
			const auto geometric=std::atan2(captured[0][1],captured[0][0])*57.29577951308232f;
			check(std::abs(std::remainder(old.yaw-geometric,360.f))>179.f &&
				std::abs(std::remainder(heading.yaw-geometric,360.f))<1.f,
				"native Euler branch remains continuous while physical body heading is corrected");
			const auto value=body.update({0,0,0},captured,heading.yaw,1,
				std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(10*i));
			check(value.valid && value.yaw_axis[0][0]*captured[0][0]+value.yaw_axis[0][1]*captured[0][1]>.94f,
				"body no longer follows the backwards Euler yaw");
		}
		for(float pitch : {-120.f,-70.f,0.f,70.f,120.f})for(float roll : {-40.f,0.f,40.f})
		{
			horizontal_heading local,world;
			check(local.update(axis(pitch,23,roll)) && world.update(axis(pitch,123,roll)) && close(world.yaw-local.yaw,100),
				"physical heading composes with artificial turns without history-dependent offsets");
		}
		auto invalid=captured;invalid[1][1]=std::numeric_limits<float>::quiet_NaN();const auto saved=heading.yaw;
		check(!heading.update(invalid) && heading.yaw==saved,"invalid orientation cannot poison physical heading history");
	}
}
