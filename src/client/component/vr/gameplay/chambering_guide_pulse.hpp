#pragma once
#include <cmath>

namespace vr::gameplay::weapons::chambering_guide
{
	struct modulation {float tint{.10f},emission{.015f};};
	inline constexpr double pulse_period_seconds=2.0;
	inline modulation pulse(double seconds) noexcept
	{
		if(!std::isfinite(seconds))return {};
		// Reduce the angle before sin so long-running sessions retain precision.
		const auto phase=std::fmod(seconds,pulse_period_seconds)/pulse_period_seconds;
		const auto wave=static_cast<float>(.5+.5*std::sin(phase*6.283185307179586));
		return {.10f+.08f*wave,.015f+.025f*wave};
	}
}
