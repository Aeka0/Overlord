#pragma once
#include "weapon_sound_reference.hpp"

namespace vr::gameplay::weapons::native_weapon_sound_slice
{
	bool initialize();
	bool play(short entity, const float* origin, const char* alias, sound_part part,const sound_window* window=nullptr);
}
