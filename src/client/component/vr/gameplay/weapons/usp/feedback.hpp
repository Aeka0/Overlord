#pragma once
#include "../../detachable_magazine.hpp"
#include "../../weapon_sound_reference.hpp"

namespace vr::gameplay::weapons::usp
{
	// H2 USP tactical notetracks, resolved through its LIVE WeaponDef sound map.
	// H1 pose clips do not authorize H1-only sound keys. The close key really is
	// named M9 in the exported H2 USP reload_empty; never fall back to M9's map.
	// Native capture: close -> weap_usp45_chamber1_plr; rear -> weap_usp45_chamber_plr.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_usp45_clipout_plr";
		case magazine_in: return "weap_usp45_clipin_plr";
		case action_rear: return "pull_slide";
		case action_close: return "weap_m9_chamber_plr";
		default: return nullptr;
		}
	}
	inline sound_reference sound_override(mechanics::effect kind) noexcept
	{
		// Clip-out contains both motions. Replace the substitute clip-in with
		// its second segment, using the same live USP map as magazine removal.
		return kind==mechanics::effect::magazine_in ?
			sound_reference{sound_key(mechanics::effect::magazine_out),sound_reference_kind::notetrack,sound_part::second} : sound_reference{};
	}
}
