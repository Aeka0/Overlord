#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::tmp
{
	// Native WeaponDef resolves these exported MP9 keys. The chamber clip is
	// compound: play it once per completed stroke, not again at the rear stop.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_mp9_clipout_plr";
		case magazine_in: return "weap_mp9_clipin_plr";
		case action_close: return "weap_mp9_chamber_plr";
		default: return nullptr;
		}
	}
}
