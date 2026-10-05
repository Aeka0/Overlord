#pragma once
#include <cmath>

namespace vr::gameplay::weapons
{
	inline void advance_hinge_travel(float& travel,float& previous,float angle,float stroke)noexcept
	{
		travel+=std::remainder(angle-previous,6.283185307f)/stroke;
		previous=angle; // Keep overtravel at either physical stop until the grasp ends.
	}
}
