#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::mp5
{
	// Keys verified in the loaded primary WeaponDef notetrack map.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_mp5sd_clipout_plr";
		case magazine_in: return "weap_mp5sd_clipin_plr";
		case action_rear: return "weap_mp5sd_chamber_plr";
		case action_close: return "weap_mp5_hit_plr_weap";
		default: return nullptr;
		}
	}
}
