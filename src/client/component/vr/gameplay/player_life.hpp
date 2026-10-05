#pragma once
#include <cstdint>

namespace vr::gameplay::player_life
{
	// H2's server selects 5/6 for dead/free or dead/linked, then promotes
	// free death to 7 for the authored death animation (0x1404AD7A1..D803).
	inline bool dead(std::uint8_t movement_type,int health) noexcept
	{
		return (movement_type>=5 && movement_type<=7) || (movement_type<=1 && health<=0);
	}
}
