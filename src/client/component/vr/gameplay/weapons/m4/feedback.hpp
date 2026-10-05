#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::m4
{
	// Verified m4_silencer WeaponDef lookup keys. Do not infer M203 capability
	// from unrelated launcher/selector keys present in that same native map.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_m4carbine_clipout_plr";
		case magazine_in: return "weap_m4carbine_clipin_plr";
		case action_rear: return "weap_m4carbine_first_chamber_plr";
		case action_close: return "weap_m4carbine_chamber_close_plr";
		default: return nullptr;
		}
	}
}
