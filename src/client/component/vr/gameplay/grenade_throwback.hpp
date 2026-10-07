#pragma once
#include "grenade_state.hpp"

namespace vr::gameplay::grenades
{
	// Range/visibility and target selection belong to the native HUD. This is
	// only a forgiving controller-direction test (70-degree half angle).
	inline bool throwback_direction(hands::vec origin,hands::vec forward,hands::vec target,float units)noexcept
	{
		if(!std::isfinite(units) || units<=0)return false;
		for(unsigned i=0;i<3;++i)if(!std::isfinite(origin[i]) || !std::isfinite(forward[i]) || !std::isfinite(target[i]))return false;
		const auto delta=hands::sub(target,origin);
		const float distance=hands::length(delta),axis=hands::length(forward);
		if(!std::isfinite(distance) || !std::isfinite(axis) || axis<.5f || axis>1.5f)return false;
		return distance<=units*.12f || hands::dot(delta,forward)>=distance*axis*.34202014f;
	}
}
