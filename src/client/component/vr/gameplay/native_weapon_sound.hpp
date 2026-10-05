#pragma once
#include <array>
#include <cstdint>
#include "physical_reload_profile.hpp"
#include "weapon_sound_reference.hpp"

namespace vr::gameplay::weapons {struct cylinder_profile;}

namespace vr::gameplay::weapons::native_weapon_sound
{
	bool initialize();
	bool play(std::uint32_t weapon,const launcher_profile& definition,sound_reference sound,const std::array<float,3>& origin);
	bool play(std::uint32_t weapon,const tube_profile& definition,sound_reference sound,const std::array<float,3>& origin);
	bool play(std::uint32_t weapon,const cylinder_profile& definition,sound_reference sound,const std::array<float,3>& origin);
	bool play_shot(std::uint32_t weapon,const std::array<float,3>& origin);
	// The caller has validated the host/module relationship at feedback admission.
	bool play_module(std::uint32_t host,std::uint32_t module,sound_reference sound,const std::array<float,3>& origin);
	bool play_module_shot(std::uint32_t host,std::uint32_t module,const std::array<float,3>& origin);
	bool play_melee_impact(const std::array<float,3>& origin);
	bool play_quick_reload_start(std::uint32_t weapon,const std::array<float,3>& origin);
	bool play_quick_reload_start_at(const std::array<float,3>& origin);
	bool play_vehicle(std::uint32_t weapon,sound_reference sound,const std::array<float,3>& origin);
	// Offhand notetrack feedback is independent of the projected firearm/reload.
	// Suffix must select exactly one key in the current native definition.
	bool play_offhand(std::uint32_t weapon,bool release,const std::array<float,3>& origin);
	// A mission-owned physical prop has a definition without a projected weapon
	// or ammo inventory. Caller owns authorization; identity and map stay native.
	bool play_prop(std::uint32_t weapon,std::string_view native_name,const char* notetrack,const std::array<float,3>& origin);
	// Authored profiles only: resolve a semantic notetrack key through the CURRENT native
	// WeaponDef, then use the positional client weapon-sound wrapper.
	bool play(std::uint32_t weapon, const reload_profile& definition, sound_reference sound, const std::array<float, 3>& origin);
	bool play(std::uint32_t weapon, std::string_view native_name, int capacity, const char* key, const std::array<float,3>& origin);
	bool play(std::uint32_t weapon, std::string_view native_name, int capacity, sound_reference sound, const std::array<float,3>& origin);
}
