#pragma once
#include "weapon_registration.hpp"
#include "tube_profile.hpp"

namespace vr::gameplay::weapons
{
	extern const profile_capabilities<tube_profile> tube_definitions;
	inline const tube_profile* native_tube_profile(std::string_view name,int capacity,const tube_profile* scene=nullptr)noexcept
	{
		for(auto* p:tube_definitions)if((!scene || p==scene) && p->matches_native(name,capacity))return p;
		return nullptr;
	}
	inline bool native_tube_shape_supported(std::string_view name,int capacity,bool segmented,int add)noexcept
	{return segmented && add==1 && native_tube_profile(name,capacity);}
}
