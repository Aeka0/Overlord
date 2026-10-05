#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::pp2000
{
	// Native source notetrack keys, resolved through the actual weapon map.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_pp2000_clipout_plr";
		case magazine_in: return "weap_pp2000_clipin_plr";
		case action_close: return "weap_pp2000_chamber_plr";
		default: return nullptr;
		}
	}
}
