#pragma once
#include "weapon_carry_runtime.hpp"

namespace vr::gameplay::weapons::independent_fire
{
	// Controller command construction supplies the ordinary/script input gate.
	void command(bool gameplay) noexcept;
	bool owns_native(const void* player_state) noexcept;
	void update(); // Called by carry after grip/drop/switch settlement, server only.
}
