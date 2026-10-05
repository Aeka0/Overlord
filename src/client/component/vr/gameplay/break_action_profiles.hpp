#pragma once
#include "weapon_registration.hpp"
#include "break_action_profile.hpp"

namespace vr::gameplay::weapons
{
	extern const profile_capabilities<break_action_profile> break_action_definitions;
	inline const break_action_profile* native_break_action_profile(std::string_view name,int capacity,const break_action_profile* scene=nullptr)noexcept
	{for(const auto* p:break_action_definitions)if((!scene || scene==p) && p->matches_native(name,capacity))return p;return nullptr;}
}
