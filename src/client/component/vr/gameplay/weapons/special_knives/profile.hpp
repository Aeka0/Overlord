#pragma once
#include "poses.hpp"
#include "../../special_melee.hpp"
#include "../../weapon_profile_binding.hpp"

namespace vr::gameplay::weapons::special_knives
{
	inline constexpr special_melee::blade ending_blade{{.8f, 0, 0}, {11.14f, 0, -.39f}};
	inline constexpr special_melee::blade bayonet_blade{{0, 0, 3.9f}, {0, 0, 11.3f}};
	inline bool suppress_equip(std::string_view name) noexcept
	{
		return name.starts_with("h2_wpn_melee_knife_") || name.starts_with("h1_wpn_melee_bayonet_knife_");
	}
	inline const profile ending = []
	{
		profile p{
		    .id = "ending_knife",
		    .receiver = "viewmodel_commando_knife",
		    .variant = "clean",
		    .aiming = aim_rule::rear_hand,
		    .wrists = ending_wrists,
		    .authored_rear = 1,
		    .acquire_meters = profile_defaults::acquire_meters,
		    .release_meters = profile_defaults::release_meters,
		    .blend_seconds = profile_defaults::blend_seconds,
		    .fingers = ending_fingers,
		    .equip_rest = ending_rest,
		    .suppress_equip = suppress_equip,
		};
		p.melee = &ending_blade;
		p.support_enabled = false;
		return p;
	}();
	inline const profile bloody = []
	{
		auto p = ending;
		p.id = "ending_knife_bloody";
		p.receiver = "viewmodel_commando_knife_bloody";
		p.variant = "bloody";
		return p;
	}();
	inline const profile bayonet = []
	{
		profile p{
		    .id = "h2_cheatcommandoknife",
		    .receiver = "wpn_h1_melee_rifle_bayonet_vm",
		    .variant = "bayonet",
		    .aiming = aim_rule::rear_hand,
		    .wrists = bayonet_wrists,
		    .authored_rear = 1,
		    .acquire_meters = profile_defaults::acquire_meters,
		    .release_meters = profile_defaults::release_meters,
		    .blend_seconds = profile_defaults::blend_seconds,
		    .fingers = bayonet_fingers,
		    .equip_rest = bayonet_rest,
		    .suppress_equip = suppress_equip,
		};
		p.melee = &bayonet_blade;
		p.support_enabled = false;
		p.viewmodel.visibility = part_visibility::rigid_groups;
		return p;
	}();
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		const auto* p = receiver.name == ending.receiver    ? &ending
		                : receiver.name == bloody.receiver  ? &bloody
		                : receiver.name == bayonet.receiver ? &bayonet
		                                                    : nullptr;
		if (!p || receiver.count != special_melee::bone_count(receiver.name) ||
		    bones.size() != std::size_t(rig.count) || rig.muzzle != -1)
			return {nullptr, "special knife skeleton unavailable"};
		for (int i = 0; i < receiver.count; ++i)
		{
			const auto name = i == 0                    ? special_melee::root(receiver.name)
			                  : i == receiver.count - 1 ? "tag_knife_fx"
			                                            : "tag_clip";
			if (bones[receiver.begin + i].name != name || (i && bones[receiver.begin + i].parent != rig.gun))
				return {nullptr, "special knife skeleton changed"};
		}
		auto match = bind_profile_attachments(*p, models, receiver, rig, bones);
		const auto hidden = special_melee::hidden_bone(receiver.name);
		if (match.value && hidden >= 0)
			match.hidden[(receiver.begin + hidden) / 32] |= 0x80000000u >> ((receiver.begin + hidden) % 32);
		return match;
	}
}
