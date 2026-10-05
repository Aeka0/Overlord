#pragma once
#include "reload_profile.hpp"
#include "../../weapon_actions.hpp"
#include "../attachments/rifle.hpp"

namespace vr::gameplay::weapons::aug
{
	inline constexpr std::array<assembly_attachment, 4> specific_attachments{
	    {{{"attach_h2_steyr_foregrip_vm_arctic", "tag_foregrip", "tag_foregrip"},
	      attachment_role::foregrip,
	      1},
	     {{"attach_h2_steyr_rail_vm", "tag_steyr_rail", "tag_steyr_rail"}, attachment_role::cover, 1},
	     {{"attach_h2_steyr_scope_vm", "tag_steyr_scope", "tag_steyr_scope"}, attachment_role::optic, 4},
	     {{"attach_h2_steyr_foregrip_vm", "tag_foregrip", "tag_foregrip"}, attachment_role::foregrip, 1}}};
	inline constexpr auto attachments = rifle_attachments::with_common(specific_attachments);
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		return base_equip_action(animation, "h2_wpn_asl_aug_grip_");
	}
	inline const std::array<profile, skins.size()> assemblies = []
	{
		std::array<profile, skins.size()> out;
		for (size_t skin = 0; skin < skins.size(); ++skin)
			out[skin] = {
			    .id = "aug",
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
		if (skin < 0 || receiver.count != 18)
			return {nullptr, "aug receiver contract rejected"};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "aug attachment topology or cardinality rejected"};
		// Live composites require a foregrip and either rail or native scope.
		bool scope{};
		for (const auto& m : models)
			scope |= m.name == specific_attachments[2].contract.model;
		if (bound.counts[static_cast<size_t>(attachment_role::foregrip)] != 1 ||
		    bound.counts[static_cast<size_t>(attachment_role::cover)] != (scope ? 0u : 1u))
			return {nullptr, "AUG foregrip / rail / scope assembly incomplete"};
		return {&assemblies[skin], "aug complete assembly matched", {}, bound.muzzle};
	}
}
