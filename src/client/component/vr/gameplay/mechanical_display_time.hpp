#pragma once
#include "../controller_input.hpp"
#include <algorithm>

namespace vr::gameplay::weapons
{
	// Render-only evaluation between authoritative input ticks. Stop predicting
	// on stale samples; presentation cannot commit ammo, sound or hand ownership.
	inline float mechanical_display_seconds(controller_input::clock::time_point sample,
		controller_input::clock::time_point now)noexcept
	{
		if(now<sample || now-sample>std::chrono::milliseconds(150))return 0;
		return std::min(.05f,std::chrono::duration<float>(now-sample).count());
	}
}
