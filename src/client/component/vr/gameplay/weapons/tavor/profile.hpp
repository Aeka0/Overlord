#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::tavor
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		return base_equip_action(animation, "h2_wpn_asl_tavor_");
	}
	inline const std::array<profile, skins.size()> assemblies = []
	{
		std::array<profile, skins.size()> out;
		for (size_t i = 0; i < skins.size(); ++i)
			out[i] = {
			    .id = "tavor",
			    .receiver = skins[i]->rigid_magazine_source,
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
			    .reload = skins[i],
			    .viewmodel =
			        {
			            .visibility = part_visibility::rigid_groups,
			        },
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
	inline constexpr auto attachments = rifle_attachments::with_common(std::array<assembly_attachment, 1>{
	    {{{"attach_h2_tavor_scope_vm", "tag_tavor_scope", "tag_tavor_scope"}, attachment_role::optic, 2}}});
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		const int skin = receiver_skin(receiver.name);
		if (skin < 0 || receiver.count != 17)
			return {nullptr, "TAR-21 receiver contract rejected"};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "TAR-21 attachment topology or cardinality rejected"};
		return {&assemblies[skin], "TAR-21 primary magazine feed matched", {}, bound.muzzle};
	}
}
