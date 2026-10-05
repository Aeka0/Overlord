#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::m9
{
	// Semantic keys from native M9 animation notetracks, not sound filenames.
	// Resolve their values from the live weapon's sound map on each event.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_m9_clipout_plr";
		case magazine_in: return "weap_m9_clipin_plr";
		case action_rear: return "wpn_h1_m9_ins_pull";
		case action_close: return "weap_m9_chamber_plr";
		default: return nullptr; // Native firing owns its sound; grabs are tactile only.
		}
	}
}
