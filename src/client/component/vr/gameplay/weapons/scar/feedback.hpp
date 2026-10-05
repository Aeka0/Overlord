#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::scar
{
	// Source reload / pullout_first notetracks and captured native sound map.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: case magazine_take: return "weap_scar_clipout_plr";
		case magazine_in: return "weap_scar_clipin_plr";
		// Native chamber alias contains the cycle: play only on completion.
		case action_close: return "weap_scar_chamber_plr";
		default: return nullptr;
		}
	}
}
