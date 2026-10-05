#pragma once
#include "weapon_carry.hpp"
#include "special_melee.hpp"
#include <string_view>

namespace vr::gameplay::weapons::carry
{
	inline rules profile_for(std::string_view name) noexcept
	{
		if(special_melee::supported(name))return {true,false};
		if(name=="usp_laserdesignator")return {false,true,true};
		for(const std::string_view launcher:{"at4","stinger","javelin"})
			if(name==launcher || (name.starts_with(launcher) && name.size()>launcher.size() && name[launcher.size()]=='_'))
				return {false,false,false,true};
		// Authored size/ownership policy. A native SMG category is not a waist permit.
		for (const auto pistol:{"beretta","colt45","deserteagle","usp","glock","coltanaconda"})
		{
			const std::string_view base{pistol};
			if (name==base || (name.starts_with(base) && name.size()>base.size() && name[base.size()]=='_')) return {true,true};
		}
		for (const auto compact:{"tmp","pp2000","uzi","beretta393","ranger"})
		{
			const std::string_view base{compact};
			if (name==base || (name.starts_with(base) && name.size()>base.size() && name[base.size()]=='_')) return {true,false};
		}
		return {};
	}
}
