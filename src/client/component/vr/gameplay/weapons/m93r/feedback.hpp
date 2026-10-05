#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::m93r
{
	// These M9-named keys are actually authored in M93R reload notetracks.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_m9_clipout_plr";
		case magazine_in: return "weap_m9_clipin_plr";
		case action_close: return "weap_m9_chamber_plr";
		default: return nullptr;
		}
	}
}
