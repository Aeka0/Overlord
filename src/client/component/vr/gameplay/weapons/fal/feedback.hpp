#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::fal
{
	// Exported notetrack keys; the actual native weapon resolves the aliases.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: case magazine_take: return "weap_fnfal_clipout_plr";
		case magazine_in: return "weap_fnfal_clipin_plr";
		// Native chamber key is a compound cycle; play it once on completion.
		case action_close: return "weap_fnfal_chamber_plr";
		default: return nullptr;
		}
	}
}
