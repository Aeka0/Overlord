#pragma once
#include "shot_geometry.hpp"
#include <string>

namespace vr::gameplay::aim_assist
{
	enum class outcome { disabled, no_target, applied, query_failed };
	enum class shot_route { projected, independent };
	// Call only on the native server firing thread with an admitted, frozen shot.
	// No pointers or VM objects survive the call; failure leaves geometry intact.
	outcome apply(weapons::shot_geometry& shot, float strength, unsigned shooter,
		shot_route route=shot_route::projected) noexcept;
	// Shared by both shot owners; these are evaluations, not confirmed hits.
	std::string status();
}
