#pragma once
#include "binding.hpp"

namespace game
{
	struct gentity_s;
}

namespace vr::h2
{
	// Engine object identity only. Inspect a reviewed layout through the domain's
	// existing bounded/locked view; this pointer alone grants no bone/asset access.
	struct dobj;
	using weapon_token = std::uint32_t;

	namespace sp
	{
		// Current supported MW2CR SP image, loaded at the established H2 base.
		// This table declares ABI facts. It performs no scanning, admission,
		// caching of dynamic state or installation of hooks.
		inline constexpr function<dobj*(const game::gentity_s*)> server_entity_dobj{0x1405A6ED0};
		inline constexpr function<dobj*(int entity_handle, int local_client)> client_entity_dobj{0x1405A6DD0};
		inline constexpr function<void(const float* native_angles, float* quaternion)> angles_to_quaternion{
		    0x140613590};
		// player_state is the existing opaque native player-state/client prefix.
		// Do not replace the live query with WeaponDef::clipSize: native modifiers
		// and alternate-mode interpretation remain engine-owned.
		inline constexpr function<int(const void* player_state, weapon_token weapon, bool alternate)>
		    clip_capacity{0x1406A3A60};
		inline constexpr function<int(weapon_token weapon, bool alternate)> weapon_type{0x1406A5440};
		inline constexpr function<int(weapon_token weapon, bool alternate)> weapon_inventory_type{
		    0x1406A5360};
		// Retain the existing native integer/zero interpretation, not a guessed
		// enum or a broader admission of paired weapons.
		inline constexpr function<int(weapon_token weapon)> weapon_dual_wield_flag{0x1406A5610};

		// FinishMove copies this REQUEST into usercmd.weapon. It does not prove
		// completion of weapon switching or replace the actual playerState weapon.
		inline constexpr read_only_global<weapon_token> weapon_selection_request{0x141E8A628};
	}
}
