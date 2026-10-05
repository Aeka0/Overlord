#pragma once
#include "weapon_interaction.hpp"

namespace vr::gameplay::weapons
{
	// Only the four vectors proven at H2 0x140518880/0x1404AAD60.
	// The following 0x30..0x4f bytes belong to the native weapon descriptor.
	struct shot_geometry
	{
		hands::vec forward, right, up, origin;
	};
	static_assert(sizeof(shot_geometry) == 0x30);
	inline shot_geometry geometry(const muzzle_frame& value) noexcept
	{
		return {value.axis[0], hands::scale(value.axis[1], -1), value.axis[2], value.position};
	}
}
