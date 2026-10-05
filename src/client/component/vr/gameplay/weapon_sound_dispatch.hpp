#pragma once
#include "weapon_sound_reference.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace vr::gameplay::weapons
{
	// Profiles admit a family/explicit variants; native playback must retain the
	// exact observed name and weapon token even when a sound explicitly names
	// another loaded WeaponDef as its shared notetrack source.
	template<class Profile,class Emit> bool dispatch_profile_sound(const Profile& profile,
		std::uint32_t requested,std::uint32_t observed,std::string_view native_name,int capacity,bool valid,
		sound_reference sound,const std::array<float,3>& origin,Emit&& emit)
	{
		if(!valid || !requested || requested>=512 || requested!=observed || !profile.matches_native(native_name,capacity))return false;
		return emit(observed,native_name,capacity,sound,origin);
	}
}
