#pragma once
#include "falling_trajectory.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	using hands::vec;
	inline float segment_distance(vec point, vec start, vec end) noexcept
	{
		const auto axis = hands::sub(end,start);
		const float length2 = hands::dot(axis,axis);
		const float t = length2 > 1e-8f ? std::clamp(hands::dot(hands::sub(point,start),axis)/length2,0.f,1.f) : 0;
		return hands::length(hands::sub(point,hands::add(start,hands::scale(axis,t))));
	}
	inline float box_distance(vec point, vec low, vec high) noexcept
	{
		vec closest{};
		for (int i=0;i<3;++i) closest[i] = std::clamp(point[i],low[i],high[i]);
		return hands::length(hands::sub(point,closest));
	}
	// A finite mouth volume, not a single frame-sensitive plane. Caller rejects
	// stale/teleported/misaligned trajectories and spawning directly in contact;
	// a later valid contact has no extra approach-direction or dwell requirement.
	inline bool sweep_well(vec from, vec to, float radius, float depth, float below = -1) noexcept
	{
		if (below < 0) below = depth;
		float begin=0, end=1;
		const auto d = hands::sub(to,from);
		if (std::abs(d[2]) < 1e-7f)
		{ if (from[2] < -below || from[2] > depth) return false; }
		else
		{
			const float a=(-below-from[2])/d[2], b=(depth-from[2])/d[2];
			begin=std::max(begin,std::min(a,b)); end=std::min(end,std::max(a,b));
		}
		const float a=d[0]*d[0]+d[1]*d[1];
		const float b=2*(from[0]*d[0]+from[1]*d[1]);
		const float c=from[0]*from[0]+from[1]*from[1]-radius*radius;
		if (a < 1e-10f) { if (c > 0) return false; }
		else
		{
			const float discriminant=b*b-4*a*c;
			if (discriminant < 0) return false;
			const float root=std::sqrt(discriminant);
			begin=std::max(begin,(-b-root)/(2*a)); end=std::min(end,(-b+root)/(2*a));
		}
		return begin <= end;
	}
	using motion::translate_local;
	// A fixed wrist orientation/offset on a one-dimensional moving part.
	inline hands::anchor part_wrist(hands::anchor rest, vec axis, float travel, float maximum) noexcept
	{
		rest.position = hands::add(rest.position,hands::scale(axis,std::clamp(travel,0.f,maximum)));
		return rest;
	}
	inline vec well_exit_translation(hands::anchor seated_local, vec top_local, vec well_local, float clearance) noexcept
	{
		const auto mouth = hands::rotate(hands::conjugate(seated_local.rotation),hands::sub(well_local,seated_local.position));
		const auto delta = hands::sub(mouth,top_local);
		const auto distance = hands::length(delta);
		return distance > 1e-6f ? hands::scale(delta,1+clearance/distance) : vec{};
	}
	inline vec cartridge_exit_velocity(hands::anchor port, float units) noexcept
	{
		// tag_brass already rotates +X out of the real ejection port. Applying
		// gun-local -Y again would turn a right-side ejection into rearward motion.
		return hands::rotate(port.rotation,{1.5f*units,0,0});
	}
	using motion::free_drop;
}
