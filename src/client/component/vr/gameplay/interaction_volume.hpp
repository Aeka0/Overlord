#pragma once
#include "world_interaction_policy.hpp"

namespace vr::gameplay::interaction
{
	// Native broadphase/distance tests use entity origins. Cover the maximum
	// admitted model envelope there, then apply the actual reach to model bounds.
	inline constexpr float model_origin_margin_m=2.f;
	struct oriented_volume
	{
		vec origin{},center{},half{};
		std::array<vec,3> axis{{{1,0,0},{0,1,0},{0,0,1}}};
		bool valid{};
		vec world(vec local) const noexcept
		{
			auto out=origin;for (unsigned n=0;n<3;++n) out=hands::add(out,hands::scale(axis[n],local[n]));return out;
		}
		vec local_vector(vec value) const noexcept
		{return {hands::dot(value,axis[0]),hands::dot(value,axis[1]),hands::dot(value,axis[2])};}
	};
	inline target score_volume(const ray& aim,target_key key,const oriented_volume& volume,std::uint32_t weapon=0) noexcept
	{
		if (!volume.valid || !valid(aim)) return {};
		if (hands::length(volume.center)+hands::length(volume.half)>model_origin_margin_m*aim.units) return {};
		for (unsigned n=0;n<3;++n)
		{
			if (!std::isfinite(volume.origin[n]) || std::abs(volume.origin[n])>1e7f ||
				std::abs(hands::dot(volume.axis[n],volume.axis[n])-1)>.002f) return {};
			for (float x:volume.axis[n]) if (!std::isfinite(x)) return {};
			for (unsigned j=0;j<n;++j) if (std::abs(hands::dot(volume.axis[n],volume.axis[j]))>.002f) return {};
		}
		auto local=aim;
		local.origin=volume.local_vector(hands::sub(aim.origin,volume.origin));
		local.head=volume.local_vector(hands::sub(aim.head,volume.origin));
		local.forward=volume.local_vector(aim.forward);
		auto result=score_model(local,key,volume.center,volume.half,weapon);
		if (result) result.position=volume.world(result.position);
		return result;
	}
}
