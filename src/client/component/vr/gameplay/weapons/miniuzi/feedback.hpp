#pragma once
#include "../../detachable_magazine.hpp"
#include "../../weapon_sound_reference.hpp"

namespace vr::gameplay::weapons::miniuzi
{
	inline sound_reference sound_override(mechanics::effect kind) noexcept
	{
		// Live uzi WeaponDef emptyFireSoundPlayer (0x298), 2026-09-09.
		// This is a loaded alias, not a notetrack key or a full cocking clip.
		return kind==mechanics::effect::dry_fire ?
			sound_reference{"wpn_dryfire_smg_plr",sound_reference_kind::alias} : sound_reference{};
	}
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_miniuzi_clipout_plr";
		case magazine_in: return "weap_miniuzi_clipin_plr";
		case action_close: return "weap_miniuzi_chamber_plr";
		default: return nullptr;
		}
	}
}
