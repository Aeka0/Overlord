#pragma once
#include <algorithm>
#include <cmath>

namespace vr::gameplay::npc_collision
{
	inline constexpr unsigned actor_contents=0xc000;
	inline constexpr float player_radius=9.f;
	// Only horizontal player bounds change. Never enlarge a narrow stance or
	// alter floor/ceiling clearance, the NPC bounds, or a zero-sized ray.
	template<class Bounds> bool narrow(Bounds& bounds) noexcept
	{
		for (unsigned i=0;i<3;++i)
			if (!std::isfinite(bounds.midPoint[i]) || !std::isfinite(bounds.halfSize[i]) ||
				bounds.halfSize[i]<=0 || bounds.halfSize[i]>128.f) return false;
		if (bounds.halfSize[0]<=player_radius || bounds.halfSize[1]<=player_radius) return false;
		bounds.halfSize[0]=bounds.halfSize[1]=player_radius;
		return true;
	}
	template<class Trace> bool valid(const Trace& trace) noexcept
	{return std::isfinite(trace.fraction) && trace.fraction>=0 && trace.fraction<=1;}
	template<class Trace> bool blocking(const Trace& trace) noexcept
	{return trace.startsolid || trace.allsolid || trace.fraction<1;}
	// The full-sized environment sweep always wins on a tie or initial overlap.
	// A failed supplemental trace leaves the original native result intact.
	template<class Trace> Trace merge(const Trace& original,const Trace& environment,const Trace& contact) noexcept
	{
		if (!valid(original) || !valid(environment) || !valid(contact)) return original;
		if (environment.startsolid || environment.allsolid) return environment;
		if (contact.startsolid || contact.allsolid) return contact;
		return environment.fraction<=contact.fraction ? environment : contact;
	}
}
