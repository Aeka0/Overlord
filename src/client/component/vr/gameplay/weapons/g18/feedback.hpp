#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::g18
{
	// Exported notification keys, resolved through this native WeaponDef only.
	// The accepted manual rear-stop event owns the pullout chamber cue; it is
	// emitted once per full stroke, independently of native automatic slide recoil.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_glock_clipout_plr";
		case magazine_in: return "weap_glock_clipin_plr";
		case action_rear: return "weap_glock_first_lift_chamber_plr";
		case action_close: return "weap_glock_chamber_plr";
		default: return nullptr;
		}
	}
}
