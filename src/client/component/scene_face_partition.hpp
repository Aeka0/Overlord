#pragma once
#include <cstdint>
#include <span>

namespace scene_models
{
	// Inclusive ranges in the source surface's native triangle order.
	struct surface_face_range { unsigned surface,first,last; };
	inline constexpr bool face_in_partition(std::span<const surface_face_range> ranges,unsigned surface,unsigned face) noexcept
	{for(const auto& r:ranges)if(r.surface==surface && face>=r.first && face<=r.last)return true;return false;}
	inline constexpr bool valid_face_partition(std::span<const surface_face_range> ranges,std::span<const unsigned> counts) noexcept
	{
		if(ranges.empty() || ranges.size()>256 || counts.empty() || counts.size()>255)return false;
		unsigned surface{},last{};bool first=true;
		for(const auto& r:ranges)
		{
			if(r.surface>=counts.size() || r.first>r.last || r.last>=counts[r.surface] ||
				(!first && (r.surface<surface || (r.surface==surface && r.first<=last))))return false;
			surface=r.surface;last=r.last;first=false;
		}
		return true;
	}
}
