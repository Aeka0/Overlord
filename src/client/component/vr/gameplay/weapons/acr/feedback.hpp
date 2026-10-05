#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::acr
{
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_masada_clipout_plr";
		case magazine_in: return "weap_masada_clipin_plr";
		case action_close: return "weap_masada_chamber_plr";
		default: return nullptr;
		}
	}
}
