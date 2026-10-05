#pragma once
#include "../../detachable_magazine.hpp"
#include <string_view>

namespace vr::gameplay::weapons::tmp
{
	// M4-style magazine/bolt controls; native automatic fire stays native.
	inline constexpr const char* native_name = "tmp";
	inline bool native_variant(std::string_view name) noexcept
	{
		return name == native_name || name == "tmp_reflex";
	}
	// Live H2 WeaponDef base clip size is 32; the chamber permits 32+1.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 32,
	    .release = mechanics::magazine_release::button,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
	inline constexpr auto chamber_rules = mechanics::chamber_rules(reload_rules);
}
