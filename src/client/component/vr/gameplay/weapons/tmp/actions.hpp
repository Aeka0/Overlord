#pragma once
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::tmp
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_pst_mp9_"); }
}
