#pragma once
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::g18
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_pst_glock_"); }
}
