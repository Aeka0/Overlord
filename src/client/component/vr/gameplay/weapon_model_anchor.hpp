#pragma once
#include <array>
#include <cmath>

namespace vr::gameplay::weapons
{
	// Copy of the solved bone's MODEL coordinates, not a second firing lease.
	// H2 later adds the scene's skinned placement origin, which can differ from
	// the mutable view offset used to solve this skeleton. Never reconstruct the
	// model position by subtracting a later view offset from a world-space pose.
	struct model_anchor
	{
		bool valid{};
		std::array<float, 3> position{}, solve_origin{};
	};
	inline bool valid_model_anchor(const model_anchor& value) noexcept
	{
		if (!value.valid) return false;
		for (unsigned c = 0; c < 3; ++c)
			if (!std::isfinite(value.position[c]) || std::abs(value.position[c]) > 1e7f ||
				!std::isfinite(value.solve_origin[c]) || std::abs(value.solve_origin[c]) > 1e7f) return false;
		return true;
	}
	// Native skinned placement has identity rotation and unit scale. This is
	// presentation reconstruction only; controller-world shots stay unchanged.
	inline std::array<float, 3> place_model_anchor(const model_anchor& value,
		const std::array<float, 3>& scene_placement) noexcept
	{
		return {value.position[0] + scene_placement[0], value.position[1] + scene_placement[1],
			value.position[2] + scene_placement[2]};
	}
}
