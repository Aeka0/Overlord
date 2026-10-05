#pragma once
#include "reload_profile.hpp"
#include "secondary_motion.hpp"
#include "../../weapon_actions.hpp"
#include "../attachments/rifle.hpp"

namespace vr::gameplay::weapons::mp5
{
	inline constexpr auto& attachments = rifle_attachments::common;
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		return base_equip_action(animation, "h2_wpn_smg_mp5k_");
	}
	inline const std::array<profile, skins.size()> assemblies = []
	{
		std::array<profile, skins.size()> out;
		for (size_t skin = 0; skin < skins.size(); ++skin)
			out[skin] = {
			    .id = "mp5",
			    .receiver = skins[skin]->rigid_magazine_source,
			    .variant = "support",
			    .aiming = aim_rule::two_hand,
			    .wrists = wrists,
			    .authored_rear = 1,
			    .acquire_meters = profile_defaults::acquire_meters,
			    .release_meters = profile_defaults::release_meters,
			    .blend_seconds = profile_defaults::blend_seconds,
			    .fingers = idle_fingers,
			    .equip_rest = equip_rest,
			    .suppress_equip = suppress_equip,
			    .reload = skins[skin],
			    .viewmodel =
			        {
			            .visibility = part_visibility::rigid_groups,
			        },
			    .cylinder = nullptr,
			    .free_hand_reference = nullptr,
			    .secondary_motion = &strap,
			};
		return out;
	}();
	inline int receiver_skin(std::string_view name) noexcept
	{
		for (size_t i = 0; i < skins.size(); ++i)
			if (name == skins[i]->rigid_magazine_source)
				return static_cast<int>(i);
		return -1;
	}
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		const int skin = receiver_skin(receiver.name);
		if (skin < 0 || receiver.count != 21)
			return {nullptr, "mp5 receiver contract rejected"};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "mp5 attachment topology or cardinality rejected"};
		return {&assemblies[skin], "mp5 complete assembly matched", {}, bound.muzzle};
	}
}
