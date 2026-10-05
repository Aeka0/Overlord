#pragma once
#include "poses.hpp"
#include "foregrip_poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::scar
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		for (auto prefix : {"h2_wpn_asl_scar_h_",
		                    "h2_wpn_asl_scar_h_shotgun_",
		                    "h2_wpn_asl_scar_h_gl_",
		                    "h2_wpn_asl_scar_h_fgrip_"})
			if (base_equip_action(animation, prefix))
				return true;
		return false;
	}
	inline constexpr auto shotgun_pose = []
	{
		auto out = idle_fingers;
		for (auto& finger : out)
			for (const auto& support : shotgun_fingers)
				if (finger.name == support.name)
					finger.rotation = support.rotation;
		return out;
	}();
	inline constexpr auto launcher_pose = []
	{
		auto out = idle_fingers;
		for (auto& finger : out)
			for (const auto& support : launcher_fingers)
				if (finger.name == support.name)
					finger.rotation = support.rotation;
		return out;
	}();
	inline const profile bare{
	    .id = "scar",
	    .receiver = "h2_viewmodel_scar_h_base",
	    .variant = "bare",
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
	inline const profile shotgun = []
	{
		auto p = bare;
		p.variant = "shotgun";
		p.wrists[0] = shotgun_support;
		p.fingers = shotgun_pose;
		p.acquire_meters = .13f;
		p.release_meters = .26f;
		p.free_hand_reference = &wrists;
		return p;
	}();
	inline const profile grenadier = []
	{
		auto p = bare;
		p.variant = "grenadier";
		p.wrists[0] = launcher_support;
		p.fingers = launcher_pose;
		p.free_hand_reference = &wrists;
		return p;
	}();
	inline constexpr auto foregrip_pose = []
	{
		auto out = idle_fingers;
		for (auto& finger : out)
			for (const auto& support : foregrip_fingers)
				if (finger.name == support.name)
					finger.rotation = support.rotation;
		return out;
	}();
	inline const profile foregrip = []
	{
		auto p = bare;
		p.variant = "foregrip";
		p.wrists[0] = foregrip_support;
		p.fingers = foregrip_pose;
		p.free_hand_reference = &wrists;
		return p;
	}();
	inline constexpr auto attachments = rifle_attachments::with_common(std::array<assembly_attachment, 3>{
	    {{{"attach_h2_shotgun_vm", "tag_shotgun", "tag_shotgun"}, attachment_role::shotgun, 5},
	     {{"attach_h2_m203_vm", "tag_m203", "tag_m203"}, attachment_role::launcher, 7},
	     {{"attach_h2_scar_foregrip_vm", "tag_foregrip", "tag_foregrip"}, attachment_role::foregrip, 1}}});
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.name != bare.receiver || receiver.count != 19)
			return {nullptr, "SCAR receiver contract rejected"};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "SCAR attachment topology or cardinality rejected"};
		const auto shot = bound.counts[static_cast<size_t>(attachment_role::shotgun)];
		const auto gl = bound.counts[static_cast<size_t>(attachment_role::launcher)];
		const auto grip = bound.counts[static_cast<size_t>(attachment_role::foregrip)];
		if (shot + gl + grip > 1)
			return {nullptr, "SCAR underbarrels are mutually exclusive"};
		return {shot   ? &shotgun
		        : gl   ? &grenadier
		        : grip ? &foregrip
		               : &bare,
		        "SCAR rifle / support attachment matched",
		        {},
		        bound.muzzle};
	}
}
