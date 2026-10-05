#pragma once
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::usp
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		return base_equip_action(animation,"h2_wpn_pst_usp_tactical_") ||
			animation == "h2_wpn_pst_usp_tactical_pullout1" ||
			animation == "h2_wpn_pst_usp_tactical_pullout2" ||
			animation == "h2_wpn_pst_usp_tactical_pullout_first_alt";
	}
}

