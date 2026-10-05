#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::m1911
{
	// Exported notetrack lookup keys, NOT assumed sound aliases. Resolve the
	// live WeaponDef map; missing keys are silent, never borrow M9 audio.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_m1911colt_clipout_plr";
		case magazine_in: return "weap_m1911colt_clipin_plr";
		// Both manual directions reuse the existing single-action recording.
		case action_rear: case action_close: return "weap_m1911colt_chamber_plr";
		default: return nullptr;
		}
	}
}

