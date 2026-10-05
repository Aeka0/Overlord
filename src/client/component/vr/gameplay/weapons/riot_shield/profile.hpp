#pragma once
#include "poses.hpp"
#include "geometry.hpp"
#include "../../weapon_actions.hpp"
#include "../../weapon_profile_binding.hpp"
namespace vr::gameplay::weapons::riot_shield
{
	inline bool suppress_equip(std::string_view name) noexcept
	{
		return name.starts_with("h2_wpn_eqp_riot_shield_");
	}
	inline const profile base = []
	{
		profile p{
		    .id = "riot_shield",
		    .receiver = "h2_viewmodel_riot_shield_mp",
		    .variant = "shield",
		    .aiming = aim_rule::rear_hand,
		    .wrists = wrists,
		    .authored_rear = 0,
		    .acquire_meters = profile_defaults::acquire_meters,
		    .release_meters = profile_defaults::release_meters,
		    .blend_seconds = profile_defaults::blend_seconds,
		    .fingers = idle_fingers,
		    .equip_rest = equip_rest,
		    .suppress_equip = suppress_equip,
		};
		p.defense = &defense;
		p.control_grips = &wrists;
		p.control_rotations = &orientations;
		p.free_hand_reference = &free_wrists;
		p.support_enabled = false;
		p.forearm = &arm_mount;
		return p;
	}();
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		constexpr std::array<std::string_view, 17> names{"j_gun",
		                                                 "j_strap_01",
		                                                 "j_strap_bottom",
		                                                 "j_strap_top",
		                                                 "tag_brass",
		                                                 "tag_flash",
		                                                 "tag_flash_silenced",
		                                                 "j_strap_02",
		                                                 "j_strap_03",
		                                                 "j_strap_04",
		                                                 "j_strap_05",
		                                                 "j_strap_06",
		                                                 "j_strap_07",
		                                                 "j_strap_08",
		                                                 "j_strap_09",
		                                                 "j_strap_010",
		                                                 "j_strap_011"};
		constexpr std::array<int, 17> parents{-1, 0, 0, 0, 0, 0, 0, 1, 7, 8, 9, 10, 11, 12, 13, 14, 15};
		if (receiver.count != 17 || bones.size() != std::size_t(rig.count))
			return {nullptr, "shield skeleton unavailable"};
		for (int i = 0; i < 17; ++i)
			if (bones[receiver.begin + i].name != names[i] ||
			    (i && bones[receiver.begin + i].parent != receiver.begin + parents[i]))
				return {nullptr, "shield skeleton changed"};
		return bind_profile_attachments(base, models, receiver, rig, bones);
	}
}
