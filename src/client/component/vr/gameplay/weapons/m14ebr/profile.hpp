#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/precision.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::m14ebr
{
	inline bool suppress_equip(std::string_view name) noexcept
	{
		return base_equip_action(name, "h2_wpn_sni_m14ebr_");
	}
	inline const std::array<profile, 2> assemblies = []
	{
		std::array<profile, 2> out;
		for (size_t i = 0; i < skins.size(); ++i)
			out[i] = {
			    .id = "m14ebr",
			    .receiver = skins[i]->rigid_magazine_source,
			    .variant = "handguard",
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
	inline constexpr auto attachments = rifle_attachments::with_common(std::array<assembly_attachment, 4>{{
	    {{"attach_h2_m14ebr_scope_vm", "tag_sight_on", "tag_sight_on"}, attachment_role::optic, 4},
	    {{"attach_h2_m14ebr_scope_vm_arctic", "tag_sight_on", "tag_sight_on"}, attachment_role::optic, 4},
	    {{"attach_h2_m14ebr_bipod_vm", "tag_bipods", "tag_bipods"}, attachment_role::bipod, 7},
	    precision_attachments::silencer03,
	}});
	inline int receiver_skin(std::string_view name) noexcept
	{
		for (size_t i = 0; i < skins.size(); ++i)
			if (name == skins[i]->rigid_magazine_source)
				return int(i);
		return -1;
	}
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		const int skin = receiver_skin(receiver.name);
		if (skin < 0 || receiver.count != 18)
			return {nullptr, "precision rifle receiver rejected"};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "precision rifle attachment topology rejected"};
		auto hidden = precision_attachments::physical_scope_mask(models, rig, bones);
		const auto cap = bound.counts[static_cast<size_t>(attachment_role::silencer)] ? "tag_silencer_off"
		                                                                              : "tag_silencer_on";
		for (int i = receiver.begin; i < receiver.begin + receiver.count; ++i)
			if (bones[i].name == cap)
				hidden[i / 32] |= 0x80000000u >> (i % 32);
		return {&assemblies[skin], "precision rifle and physical scope matched", hidden, bound.muzzle};
	}
} // namespace vr::gameplay::weapons::m14ebr
