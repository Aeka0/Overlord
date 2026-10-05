#pragma once
#include "part_mesh_partition.hpp"

namespace vr::gameplay::weapons
{
	// Four immutable index views over the original magazine and its authored
	// top three rounds. No duplicated vertices, invented stack pitch or frame-time slicing.
	struct magazine_fill_recipe
	{
		const char* source{};
		unsigned bones{};
		std::span<const std::array<unsigned,2>> surfaces;
		std::array<std::span<const scene_models::surface_face_range>,4> faces;
		std::array<hands::vec,4> low,high; // Source bind space, native units.
	};
	inline constexpr size_t magazine_fill_level(int rounds) noexcept
	{return rounds<=0 ? 0 : rounds>=3 ? 3 : size_t(rounds);}
	inline bool valid_magazine_fill(const magazine_fill_recipe& p) noexcept
	{
		unsigned previous{};
		for(size_t i=0;i<4;++i)
		{
			if(!valid_partition({p.source,p.bones,p.surfaces,p.faces[i],p.low[i],p.high[i]}))return false;
			unsigned count{};for(const auto& r:p.faces[i])count+=r.last-r.first+1;
			if(count<=previous)return false;
			// Every higher level retains the whole lower level (including body).
			if(i)for(const auto& r:p.faces[i-1])
			{
				unsigned next=r.first;bool covered=false;
				for(const auto& higher:p.faces[i])if(higher.surface==r.surface && higher.last>=next)
				{
					if(higher.first>next)return false;
					if(higher.last>=r.last){covered=true;break;}
					next=higher.last+1;
				}
				if(!covered)return false;
			}
			previous=count;
		}
		return true;
	}
}
