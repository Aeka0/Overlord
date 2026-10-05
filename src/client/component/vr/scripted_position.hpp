#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "scripted_camera.hpp"

namespace vr::game_view
{
	// Translation has its own lifetime: entering combat must not re-anchor the
	// player, and changing camera rotation policy must not move their head.
	class scripted_position
	{
		std::uint64_t epoch_{},reference_{};
		std::array<float,3> baseline_{};
	public:
		std::array<float,3> offset(std::uint64_t epoch,std::uint64_t reference,
			std::array<float,3> head_meters,const camera_policy& policy) noexcept
		{
			for(float x:head_meters)if(!std::isfinite(x) || std::abs(x)>100000){*this={};return {};}
			if(policy.translation==head_translation::fixed){*this={};return {};}
			if(!epoch){*this={};return head_meters;}
			if(epoch!=epoch_ || reference!=reference_){baseline_=head_meters;epoch_=epoch;reference_=reference;}
			const bool attenuated=policy.translation==head_translation::attenuated;
			const float gain=attenuated ? (std::isfinite(policy.translation_gain)?std::clamp(policy.translation_gain,0.f,1.f):.1f):1.f;
			const float limit=std::isfinite(policy.translation_limit)?std::clamp(policy.translation_limit,0.f,1.f):.05f;
			std::array<float,3> result{};float length{};
			for(unsigned i=0;i<3;++i){result[i]=(head_meters[i]-baseline_[i])*gain;length+=result[i]*result[i];}
			// Even large physical steps cannot leave the authored body cavity.
			if(attenuated && length>limit*limit)for(auto& x:result)x*=limit/std::sqrt(length);
			return result;
		}
	};
}
