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
		case cylinder::effect::open:
			return {"weap_coltanaconda_clipout_plr"};
		case cylinder::effect::fill:
			return {"weap_coltanaconda_clipin_plr"};
		case cylinder::effect::close:
			return {"weap_coltanaconda_chamber_plr"};
		// The animation event coltanaconda_shelleject is NOT a WeaponDef sound
		// key. Use the verified loaded shared casing alias once at gravity clear;
		// surface-specific impact audio belongs to future collision presentation.
		case cylinder::effect::clear:
			return {"shell_eject_pistol", sound_reference_kind::alias};
		default:
			return {};
		}
	}
	// Captured story variant: same receiver hierarchy, six-round feed and
	// WeaponDef notetrack mappings. No wildcard admission of native akimbo.
	inline constexpr std::array<std::string_view, 1> native_variants{"coltanaconda_shepherd"};
	inline constexpr cylinder_profile feed{
	    .id = "magnum44",
	    .native_name = "coltanaconda",
	    .ammunition =
	        {
	            .capacity = 6,
	        },
	    .interaction = {},
	    .swing_closed = swing_closed,
	    .swing_open = swing_open,
	    .ammo_in_cylinder = ammo_in_cylinder,
	    .loader_in_wrist = loader_in_wrist,
	    .face_in_cylinder = {{-1.f, 0, 0}, {0, .7071068f, 0, .7071068f}},
	    .loader_tip = {1.7f, 0, 0},
	    .loader_rounds = loader_rounds,
	    .loader_fingers = loader_fingers,
	    .sound_key = sound,
	    .native_variants = native_variants,
	    .assets =
	        {
	            .receiver = "h2_viewmodel_colt_anaconda_base",
	            .loader = "j_speed_loader",
	            .case_body = "j_bullet01",
	            .case_tip = "j_bullet_tip01",
	            .receiver_bones = 25,
	        },
	};
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		return base_equip_action(animation, "h2_wpn_pst_colt_anaconda_");
	}
	inline constexpr profile base{
	    .id = "magnum44",
	    .receiver = "h2_viewmodel_colt_anaconda_base",
	    .variant = "base",
	    .aiming = aim_rule::rear_hand,
	    .wrists = wrists,
	    .authored_rear = 1,
	    .acquire_meters = profile_defaults::acquire_meters,
	    .release_meters = profile_defaults::release_meters,
	    .blend_seconds = profile_defaults::blend_seconds,
	    .fingers = idle_fingers,
	    .equip_rest = equip_rest,
	    .suppress_equip = suppress_equip,
	    .reload = nullptr,
	    .viewmodel =
	        {
	            .visibility = part_visibility::rigid_groups,
	        },
	    .cylinder = &feed,
	};
}
