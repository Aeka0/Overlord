#pragma once
#include "../../detachable_magazine.hpp"

namespace vr::gameplay::weapons::vector
{
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return "weap_kriss_clipout_plr";
		case magazine_in: return "weap_kriss_clipin_plr";
		case action_close: return "weap_kriss_chamber_plr";
		default: return nullptr;
		}
	}
}
