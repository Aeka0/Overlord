#pragma once
#include "../catalog_types.hpp"

namespace game_text::locales
{
	inline constexpr catalog czech=make_catalog({
		{key::weapon_needs_chamber,u8"Zasuňte náboj do komory"},
		{key::right_stick_down,u8"Pravá páčka dolů"},
		{key::vehicle_duck,u8"{button}, abyste se přikrčili."},
	});
}
