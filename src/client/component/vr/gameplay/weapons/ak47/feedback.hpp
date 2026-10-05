#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::ak47
{
	// Exported AK notification keys, resolved ONLY through the actual native
	// weapon's map at playback. No guessed sound alias or other gun's fallback.
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: case magazine_take: return "weap_ak47_clipout_plr";
		case magazine_in: return "weap_ak47_clipin_plr";
		case action_rear: return "weap_ak47_chamber_plr";
		default: return nullptr;
		}
	}
}
