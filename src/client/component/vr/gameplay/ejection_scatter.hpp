#pragma once
#include "hands/pose_solver.hpp"
#include <cstdint>

namespace vr::gameplay::weapons::ejection_scatter
{
	struct motion {hands::vec velocity{},angular_velocity{};}; // local m/s and rad/s
	inline motion sample(std::uint64_t instance,std::uint64_t event,unsigned chamber) noexcept
	{
		// One bounded sample per cartridge, without a shared PRNG or render-thread
		// dependency. The case and bullet tip must consume the same sample.
		std::uint64_t seed=instance^(event*0x9e3779b97f4a7c15ull)^(std::uint64_t(chamber+1)*0xd6e8feb86659fd93ull);
		const auto signed_unit=[&]() {
			auto x=(seed+=0x9e3779b97f4a7c15ull);
			x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;x^=x>>31;
			return float(x>>40)*(2.f/16777215.f)-1.f;
		};
		return {{.035f*signed_unit(),.12f*signed_unit(),.10f*signed_unit()},
			{1.5f*signed_unit(),2.5f*signed_unit(),2.5f*signed_unit()}};
	}
	inline hands::anchor apply(hands::anchor ballistic,hands::quat exit_rotation,const motion& scatter,
		float age,float units) noexcept
	{
		using namespace hands;
		// Preserve the original exit path before adding any side motion/tumble.
		// A short velocity ramp then avoids a visible kink at the boundary.
		if(!std::isfinite(age) || age<=.06f || !std::isfinite(units) || units<=0)return ballistic;
		const float time=std::min(age-.06f,1.2f),ramp=.03f;
		const float drift=time-ramp*(-std::expm1(-time/ramp));
		ballistic.position=add(ballistic.position,rotate(exit_rotation,scale(scatter.velocity,drift*units)));
		const float speed=length(scatter.angular_velocity);
		if(speed>1e-6f)
		{
			const float half=speed*drift*.5f;const auto axis=scale(scatter.angular_velocity,std::sin(half)/speed);
			ballistic.rotation=normalize(multiply(ballistic.rotation,{axis[0],axis[1],axis[2],std::cos(half)}));
		}
		return ballistic;
	}
}
