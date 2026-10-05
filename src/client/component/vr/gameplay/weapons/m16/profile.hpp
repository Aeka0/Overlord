#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::m16
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		return base_equip_action(animation, "h2_wpn_asl_m16_") ||
		       base_equip_action(animation, "h2_wpn_asl_m16_gl_");
	}
	inline const profile bare{
	    .id = "m16",
	    .receiver = "h2_viewmodel_m16_base",
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
	    .reload = &physical,
	    .viewmodel =
	        {
	            .visibility = part_visibility::rigid_groups,
	        },
	};
	inline constexpr auto attached_fingers = []
	{
		auto out = idle_fingers;
		for (auto& finger : out)
			for (const auto& support : launcher_fingers)
				if (finger.name == support.name)
					finger.rotation = support.rotation;
		return out;
	}();
	inline const profile grenadier = []
	{
		auto p = bare;
		p.variant = "grenadier";
		p.wrists[0] = launcher_support;
		p.fingers = attached_fingers;
		p.free_hand_reference = &wrists;
		return p;
	}();
	inline constexpr auto attachments = rifle_attachments::with_common(std::array<assembly_attachment, 2>{
	    {{{"attach_h2_m16_armor_vm", "tag_armor", "tag_armor"}, attachment_role::cover, 1},
	     {{"attach_h2_m203_vm", "tag_m203", "tag_m203"}, attachment_role::launcher, 7}}});
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.name != bare.receiver || receiver.count != 28)
			return {nullptr, "M16 receiver contract rejected"};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "M16 attachment topology or cardinality rejected"};
		const auto launchers = bound.counts[static_cast<size_t>(attachment_role::launcher)];
		const auto covers = bound.counts[static_cast<size_t>(attachment_role::cover)];
		if (launchers && covers)
			return {nullptr, "M16 handguard cover conflicts with M203"};
		part_mask hidden{};
		if (bound.counts[static_cast<size_t>(attachment_role::optic)])
		{
			// Native m16_acog hides tag_sight_on and its j_sight_ring child. The
			// independent DObj has no selected-weapon hide state to inherit.
			int handle = -1, ring = -1;
			for (int i = receiver.begin; i < receiver.begin + receiver.count; ++i)
			{
				if (bones[i].name == "tag_sight_on")
				{
					if (handle >= 0)
						return {nullptr, "M16 duplicate carry handle"};
					handle = i;
				}
				if (bones[i].name == "j_sight_ring")
				{
					if (ring >= 0)
						return {nullptr, "M16 duplicate sight ring"};
					ring = i;
				}
			}
			if (handle < 0 || ring < 0 || rig.parent[handle] != rig.gun || rig.parent[ring] != handle ||
			    !rig.weapon_bones[handle] || !rig.weapon_bones[ring])
				return {nullptr, "M16 carry handle topology rejected"};
			for (const auto i : {handle, ring})
				hidden[i / 32] |= 0x80000000u >> (i % 32);
		}
		// No implicit vertical foregrip: bare and covered rails use native handguard
		// contact. M203 changes support only; rear grip and mechanics stay stable.
		return {launchers ? &grenadier : &bare, "M16 handguard / M203 support matched", hidden, bound.muzzle};
	}
}
