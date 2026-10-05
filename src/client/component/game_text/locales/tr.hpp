#pragma once
#include "../catalog_types.hpp"

namespace game_text::locales
{
	inline constexpr catalog turkish=make_catalog({
		{key::weapon_needs_chamber,u8"Fişeği yatağa sürün"},
		{key::right_stick_down,u8"Sağ çubuğu aşağı itin"},
		{key::vehicle_duck,u8"Eğilmek için {button}."},
	});
}
