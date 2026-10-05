#pragma once
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::miniuzi
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_smg_miniuzi_"); }
}
