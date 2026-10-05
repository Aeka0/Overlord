#pragma once
#include "special_melee.hpp"

namespace vr::gameplay::weapons
{
	enum class fire_delivery { unsupported, bullets, native_projectile, scripted_device };
	// G_FireWeapon dispatches type 1 to bullets and type 3 to launcher missiles
	// or rifle grenades. Type 2 is thrown ordnance, with a different input contract.
	inline constexpr fire_delivery native_fire_delivery(int type,std::string_view name={}) noexcept
	{
		if(special_melee::supported(name))return fire_delivery::unsupported;
		return type==1 ? fire_delivery::bullets : type==3 ? fire_delivery::native_projectile : fire_delivery::unsupported;
	}
	inline constexpr bool suppress_native_attack(fire_delivery delivery,bool independent) noexcept
	{
		// Unknown deliveries remain closed while instance fire owns the input.
		return independent && delivery!=fire_delivery::native_projectile && delivery!=fire_delivery::scripted_device;
	}
}
