#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::ump
{
	// Keys verified in the loaded primary WeaponDef notetrack map.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_ump45_clipout_plr";
		case magazine_in: return "weap_ump45_clipin_plr";
		case action_rear: return "weap_ump45_chamber_plr";
		case action_close: return "h2_wpn_ump45_close_chamber_plr";
		default: return nullptr;
		}
	}
}
