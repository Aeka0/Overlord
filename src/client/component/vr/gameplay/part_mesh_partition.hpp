#pragma once
#include "hands/pose_library.hpp"
#include "component/scene_face_partition.hpp"

namespace vr::gameplay::weapons
{
	// Immutable two-piece partition of one rigid bone group. Native surface
	// order and geometry witnesses must match before hiding the original group.
	struct part_mesh_partition
	{
		const char* source;
		unsigned bones;
		std::span<const std::array<unsigned,2>> surfaces;
		std::span<const scene_models::surface_face_range> moving_faces;
		hands::vec moving_low,moving_high;
	};
	inline bool valid_partition(const part_mesh_partition& p) noexcept
	{
		if(!p.source || !*p.source || !p.bones || p.bones>256 || p.surfaces.empty() || p.surfaces.size()>255)return false;
		std::array<unsigned,255> counts{};
		for(size_t i=0;i<p.surfaces.size();++i)
		{
			if(!p.surfaces[i][0] || p.surfaces[i][0]>65535 || !p.surfaces[i][1] || p.surfaces[i][1]>65535)return false;
			counts[i]=p.surfaces[i][1];
		}
		for(unsigned i=0;i<3;++i)if(!std::isfinite(p.moving_low[i]) || !std::isfinite(p.moving_high[i]) ||
			std::abs(p.moving_low[i])>10000 || std::abs(p.moving_high[i])>10000 || p.moving_low[i]>p.moving_high[i])return false;
		return scene_models::valid_face_partition(p.moving_faces,std::span(counts).first(p.surfaces.size()));
	}
}
