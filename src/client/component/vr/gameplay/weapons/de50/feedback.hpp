#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::de50
{
	// Exported notetrack lookup keys, NOT assumed sound aliases. Resolve the
	// live WeaponDef map; missing keys are silent, never borrow M9 audio.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_de50_clipout_plr";
		case magazine_in: return "weap_de50_clipin_plr";
		case action_rear: return "wpn_h1_deserteagle_ins_pull";
		case action_close: return "weap_de50_chamber_plr";
		default: return nullptr;
		}
	}
}

