#pragma once
#include "../hands/pose_solver.hpp"

namespace vr::gameplay::hand_interaction
{
	struct slider_result {bool valid{};float travel{};};
	// A foregrip follows both the rifle and a longitudinal slider. Direction of
	// the hand span controls aim; its length constrains the slider. This solve
	// never reads rendered/IK-corrected hands and is invariant under rigid motion.
	inline slider_result shared_slider(hands::vec rest,hands::vec axis,float start_distance,float previous_distance,
		float distance,float initial,float stroke,float breakaway,float max_step)noexcept
	{
		for(auto v:{rest,axis})for(float x:v)if(!std::isfinite(x))return {};
		for(float x:{start_distance,previous_distance,distance,initial,stroke,breakaway,max_step})if(!std::isfinite(x))return {};
		if(stroke<=0 || initial<0 || initial>stroke || start_distance<.03f || distance<.03f || breakaway<=0 || max_step<=0 || std::abs(distance-previous_distance)>max_step)return {};
		if(std::abs(hands::dot(axis,axis)-1)>.002f)return {};
		const float along=hands::dot(rest,axis);if(along<.03f)return {}; // Non-monotonic/backward layouts need another constraint.
		const float radius=hands::length(hands::add(rest,hands::scale(axis,initial)))+distance-start_distance;
		const float squared=radius*radius-(hands::dot(rest,rest)-along*along);
		if(radius<=0 || squared<0)return {};
		const float travel=std::sqrt(squared)-along;
		if(travel < -breakaway || travel > stroke+breakaway)return {};
		return {true,std::clamp(travel,0.f,stroke)};
	}
}
