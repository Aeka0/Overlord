#pragma once
#include "shot_geometry.hpp"
#include "native_ammunition.hpp"
#include "independent_fire_clock.hpp"

namespace vr::gameplay::weapons::native_ballistics
{
	// H2's fire packet, shared by native bullets/projectiles and independent bullets. The
	// three reserved floats are zero in G_FireWeapon's primary bullet route.
	struct parameters
	{
		shot_geometry shot{};
		std::byte reserved[12]{};
		std::uint32_t weapon{}, mode{};
		bool alternate{};
		std::byte padding[3]{};
		void* definition{};
	};
	static_assert(offsetof(parameters, weapon) == 0x3c);
	static_assert(offsetof(parameters, alternate) == 0x44);
	static_assert(offsetof(parameters, definition) == 0x48);
	static_assert(sizeof(parameters) == 0x50);
	inline constexpr std::uintptr_t bullet_address = 0x1404AA300;
	inline constexpr std::uintptr_t ads_spread_address = 0x1406A1670;

	enum class outcome { unavailable, invalid_context, invalid_geometry, unsupported_weapon,
		empty, obstruction, ammunition_changed, emitted, mechanical_changed };
	struct result
	{
		outcome status{outcome::unavailable};
		native_ammunition::snapshot before{}, after{};
	};
	bool initialize();
	bool firing_timing(std::uint32_t weapon,int shot_count,independent_fire::timing& out);
	using debit_observer=bool(*)(const native_ammunition::snapshot&,const native_ammunition::snapshot&,bool sustained) noexcept;
	// One synchronous native bullet transaction. Caller owns cadence, physical
	// chamber state and trigger admission. No selected weapon, akimbo state,
	// native +attack event, animation or feedback is synthesized here.
	// Requires the current server scheduler scope and exact commandTime.
	result fire_owned(std::uint32_t weapon, const shot_geometry& geometry, int command_time,debit_observer settled=nullptr);
	result fire_owned(weapon_identity, const shot_geometry& geometry, int command_time,debit_observer settled=nullptr);
}
