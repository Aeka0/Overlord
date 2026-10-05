#pragma once
#include "hands/pose_solver.hpp"
#include "../body_pose.hpp"
#include <optional>

namespace vr::gameplay::hands
{
	// Calibrated controller coordinates, before any weapon-authored wrist pose.
	// This is the same palm approximation used by directional underbarrel grips.
	inline vec controller_palm_axis(int hand) noexcept {return {0,hand?1.f:-1.f,0};}
	inline std::optional<quat> controller_in_body(quat controller,const body_pose::estimate& body) noexcept
	{
		if(!body.valid)return std::nullopt;
		float norm{};for(float x:controller){if(!std::isfinite(x))return std::nullopt;norm+=x*x;}
		if(norm<.5f || norm>1.5f)return std::nullopt;
		for(unsigned i=0;i<3;++i)
		{
			for(float x:body.yaw_axis[i])if(!std::isfinite(x))return std::nullopt;
			for(unsigned j=0;j<=i;++j)
				if(std::abs(dot(body.yaw_axis[i],body.yaw_axis[j])-(i==j?1.f:0.f))>.002f)return std::nullopt;
		}
		if(dot(cross(body.yaw_axis[0],body.yaw_axis[1]),body.yaw_axis[2])<.998f)return std::nullopt;
		return normalize(multiply(conjugate(from_axis(body.yaw_axis)),normalize(controller)));
	}
}
