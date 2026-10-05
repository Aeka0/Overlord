#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::l86
{
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch(kind)
		{
		case magazine_out: case magazine_take: return "weap_sa80_clipout_plr";
		case magazine_in: return "weap_sa80_clipin_plr";
		case action_close: return "weap_sa80_chamber_plr";
		default: return nullptr;
		}
	}
}
