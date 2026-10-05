#pragma once
#include <algorithm>
#include <cmath>

namespace scene_models
{
	// Native radius is multiplied by placement scale after the call. Keep a
	// finite world-space allowance even for malformed/tiny scales and requests.
	inline float local_radius_padding(float world_padding, float scale) noexcept
	{
		return std::isfinite(world_padding) && std::isfinite(scale) && scale >= .0001f ?
			std::clamp(world_padding,0.f,128.f)/scale : 0.f;
	}
}
