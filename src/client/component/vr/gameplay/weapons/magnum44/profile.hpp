#pragma once
#include "poses.hpp"
#include "../../cylinder_profile.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::magnum44
{
	inline sound_reference sound(cylinder::effect e) noexcept
	{
		switch (e)
		{
		case cylinder::effect::open: return {"weap_coltanaconda_clipout_plr"};
		case cylinder::effect::fill: return {"weap_coltanaconda_clipin_plr"};
		case cylinder::effect::close: return {"weap_coltanaconda_chamber_plr"};
		// The animation event coltanaconda_shelleject is NOT a WeaponDef sound
		// key. Use the verified loaded shared casing alias once at gravity clear;
		// surface-specific impact audio belongs to future collision presentation.
		case cylinder::effect::clear: return {"shell_eject_pistol",sound_reference_kind::alias};
		default: return {};
		}
	}
	// Captured story variant: same receiver hierarchy, six-round feed and
	// WeaponDef notetrack mappings. No wildcard admission of native akimbo.
	inline constexpr std::array<std::string_view,1> native_variants{"coltanaconda_shepherd"};
	inline constexpr cylinder_profile feed{
		"magnum44","coltanaconda",{6},{},swing_closed,swing_open,ammo_in_cylinder,loader_in_wrist,
		{{-1.f,0,0},{0,.7071068f,0,.7071068f}},{1.7f,0,0},loader_rounds,loader_fingers,sound,native_variants,
		{"h2_viewmodel_colt_anaconda_base","j_speed_loader","j_bullet01","j_bullet_tip01",25}};
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_pst_colt_anaconda_"); }
	inline constexpr profile base{
		"magnum44","h2_viewmodel_colt_anaconda_base","base",aim_rule::rear_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,nullptr,{part_visibility::rigid_groups},&feed};
}
