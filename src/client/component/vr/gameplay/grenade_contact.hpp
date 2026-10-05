#pragma once
#include "grenade_profile.hpp"

namespace vr::gameplay::grenades
{
	inline constexpr float pin_acquire_meters=.10f;
	inline constexpr float pin_stroke_meters=.08f;
	// The native pin shafts run along model +Y; this axis rotates with the
	// grenade, including the opposite-hand half turn. Lateral motion cannot arm.
	inline hands::vec pin_slide(hands::vec start,hands::vec tracked,float units)noexcept
	{
		if(!std::isfinite(units) || units<=0)return {};
		const float travel=tracked[1]-start[1];
		return std::isfinite(travel)?hands::vec{0,std::clamp(travel,0.f,pin_stroke_meters*units),0}:hands::vec{};
	}
	inline hands::vec pinch_point(kind type,hands::anchor wrist,hands::quat anatomical,hands::quat mirror,unsigned hand)noexcept
	{
		auto local=authored::left_pinch[unsigned(type)];
		if(hand==1)local=hands::pose_mirror::local_point(local,mirror);
		wrist.rotation=hands::normalize(hands::multiply(wrist.rotation,anatomical));
		return hands::pose_math::compose(wrist,{local,{0,0,0,1}}).position;
	}
	inline float segment_distance(hands::vec point,hands::vec start,hands::vec end)noexcept
	{
		const auto delta=hands::sub(end,start);const float squared=hands::dot(delta,delta);
		const float t=squared>1e-6f?std::clamp(hands::dot(hands::sub(point,start),delta)/squared,0.f,1.f):0;
		return hands::length(hands::sub(point,hands::add(start,hands::scale(delta,t))));
	}
}
