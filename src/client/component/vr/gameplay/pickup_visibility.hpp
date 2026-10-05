#pragma once
#include "world_interaction_policy.hpp"
#include <span>

namespace vr::gameplay::interaction
{
	// Bounded support points from actual referenced mesh vertices. A box corner
	// is not proof that any visible geometry exists there (e.g. a pistol grip).
	struct pickup_surface_samples
	{
		inline static constexpr std::array<vec,14> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},
			{1,1,1},{1,1,-1},{1,-1,1},{1,-1,-1},{-1,1,1},{-1,1,-1},{-1,-1,1},{-1,-1,-1}}};
		std::array<vec,directions.size()> points{};
		bool populated{};
		bool add(vec point) noexcept
		{
			for(float x:point)if(!std::isfinite(x) || std::abs(x)>200)return false;
			for(std::size_t i=0;i<points.size();++i)
				if(!populated || hands::dot(point,directions[i])>hands::dot(points[i],directions[i]))points[i]=point;
			populated=true;return true;
		}
		std::span<const vec> values() const noexcept {return populated?std::span<const vec>{points}:std::span<const vec>{};}
	};
	// Both observers must see the SAME real surface point. Keep contact/range/
	// angular admission and a shared per-query trace budget; never waive cover.
	template<class Visible> target visible_pickup_surface(const ray& aim,target_key key,std::uint32_t weapon,
		std::span<const vec> points,unsigned& budget,Visible&& visible)
	{
		target best;
		for(const auto& point:points)
		{
			const auto candidate=score_weapon(aim,key,point,{},weapon);
			if(!candidate || !better(candidate,best))continue;
			if(budget<2)break;
			--budget;if(!visible(aim.head,point))continue;
			--budget;if(!visible(aim.origin,point))continue;
			best=candidate;
		}
		return best;
	}
}
