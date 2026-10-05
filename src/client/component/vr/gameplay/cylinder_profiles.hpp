#pragma once
#include "weapon_registration.hpp"
#include "cylinder_profile.hpp"

namespace vr::gameplay::weapons
{
	extern const profile_capabilities<cylinder_profile> cylinder_definitions;
	inline const cylinder_profile* native_cylinder_profile(std::string_view name,int capacity,const cylinder_profile* scene=nullptr) noexcept
	{
		for(auto* p:cylinder_definitions)if((!scene || p==scene) && p->matches_native(name,capacity))return p;
		return nullptr;
	}
}
