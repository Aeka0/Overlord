#pragma once
#include "hands/pose_solver.hpp"

namespace vr::gameplay
{
	// A body-aligned box swept by a sphere. Keeping a rounded boundary avoids
	// granting diagonal reach outside the authored hand tolerance.
	struct body_reach_volume
	{
		hands::vec origin{}, low{}, high{};
		std::array<hands::vec,3> axis{{{1,0,0},{0,1,0},{0,0,1}}};
		float radius{};
		hands::vec world(hands::vec local) const noexcept
		{
			auto result=origin;
			for (unsigned n=0;n<3;++n) result=hands::add(result,hands::scale(axis[n],local[n]));
			return result;
		}
		float distance(hands::vec point) const noexcept
		{
			if (!std::all_of(point.begin(),point.end(),[](float x){return std::isfinite(x);})) return INFINITY;
			const auto delta=hands::sub(point,origin); hands::vec outside{};
			for (unsigned n=0;n<3;++n)
			{
				const auto p=hands::dot(delta,axis[n]);
				outside[n]=p-std::clamp(p,low[n],high[n]);
			}
			return hands::length(outside);
		}
		bool contains(hands::vec point) const noexcept {return radius>0 && distance(point)<=radius;}
		float box_distance(hands::vec point) const noexcept
		{
			const auto delta=hands::sub(point,origin);float score{};
			for(unsigned n=0;n<3;++n)
			{
				const float p=hands::dot(delta,axis[n]),half=(high[n]-low[n])*.5f;
				if(!std::isfinite(p) || !std::isfinite(half) || half<=0)return INFINITY;
				score=std::max(score,std::abs(p-(low[n]+high[n])*.5f)/half);
			}
			return score;
		}
	};
	// Extra reach is deliberately behind, outside and below the body anchor;
	// it must not turn ordinary forward hand motions into inventory actions.
	inline body_reach_volume waist_reach(hands::vec origin,const std::array<hands::vec,3>& axis,
		float units,unsigned side,float height,float radius) noexcept
	{
		return {origin,{-.22f*units,side ? -.20f*units : 0.f,-.24f*units},
			{0,side ? 0.f : .20f*units,height*units},axis,radius*units};
	}
}
