#pragma once
#include <array>
#include <cmath>
#include <cstddef>

namespace scene_models
{
	template<class Placement>
	bool same_placement(const std::array<float,3>& position,const std::array<float,4>& rotation,
		const Placement& placed) noexcept
	{
		float square{},facing{};
		for(unsigned n=0;n<3;++n){const float d=position[n]-placed.origin[n];square+=d*d;}
		for(unsigned n=0;n<4;++n)facing+=rotation[n]*placed.quat[n];
		return std::isfinite(square) && std::sqrt(square)<=.001f && std::isfinite(facing) && std::abs(facing)>=.99999f;
	}
	// Native skeleton identity is common; the caller supplies its own domain
	// identity predicate without making the scene service depend on gameplay.
	template<class Poses,class Skin,class Accept,class Pose>
	bool latest_skeleton_pose(const Poses& poses,size_t cursor,const Skin& skin,Accept&& accepts,Pose& out) noexcept
	{
		for(size_t n=0;n<poses.size();++n)
		{
			const auto& value=poses[(cursor+poses.size()-1-n)%poses.size()];
			if(value.object==skin.object && value.matrices==skin.matrices && value.epoch==skin.epoch && accepts(value))
			{out=value;return true;}
		}
		return false;
	}
}
