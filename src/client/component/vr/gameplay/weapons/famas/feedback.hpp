#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::famas
{
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch(kind)
		{
		case magazine_out: case magazine_take: return "weap_famas_clipout_plr";
		case magazine_in: return "weap_famas_clipin_plr";
		case action_close: return "weap_famas_chamber_plr";
		default: return nullptr;
		}
	}
}
