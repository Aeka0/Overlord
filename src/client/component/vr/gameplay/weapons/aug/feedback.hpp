#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::aug
{
	// Keys verified in the loaded primary WeaponDef notetrack map.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_styaug_clipout_plr";
		case magazine_in: return "weap_styaug_clipin_plr";
		case action_close: return "weap_styaug_chamber_plr";
		default: return nullptr;
		}
	}
}
