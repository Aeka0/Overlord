#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::ak47
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		// These three families hold the rifle. Grenade/shotty alternate actions
		// are different feeds and must not inherit primary equip suppression.
		for (auto prefix : {"h2_wpn_asl_ak47_tac_", "h2_wpn_asl_ak47_gl_", "h2_wpn_asl_ak47_shotgun_"})
			if (base_equip_action(animation, prefix))
				return true;
		return false;
	}
	inline constexpr auto gp25_pose = []
	{
		auto out = idle_fingers;
		for (auto& finger : out)
			for (const auto& support : grenadier_fingers)
				if (finger.name == support.name)
					finger.rotation = support.rotation;
		return out;
	}();
	inline constexpr auto shotgun_pose = []
	{
		auto out = idle_fingers;
		for (auto& finger : out)
			for (const auto& support : shotgun_fingers)
				if (finger.name == support.name)
					finger.rotation = support.rotation;
		return out;
	}();
	inline const auto assemblies = []
	{
		std::array<profile, skins.size() * 3> out;
		for (size_t skin = 0; skin < skins.size(); ++skin)
		{
			profile bare{
			    .id = "ak47",
			    .receiver = skins[skin]->rigid_magazine_source,
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
			    .reload = skins[skin],
			    .viewmodel =
			        {
			            .visibility = part_visibility::rigid_groups,
			        },
			};
			out[skin * 3] = bare;
			auto gl = bare;
			gl.variant = "gp25";
			gl.wrists[0] = grenadier_support;
			gl.fingers = gp25_pose;
			gl.free_hand_reference = &wrists;
			out[skin * 3 + 1] = gl;
			auto shotgun = bare;
			shotgun.variant = "shotgun";
			shotgun.wrists[0] = shotgun_support;
			shotgun.fingers = shotgun_pose;
			shotgun.free_hand_reference = &wrists;
			out[skin * 3 + 2] = shotgun;
		}
		return out;
	}();
	inline constexpr auto attachments = rifle_attachments::with_common(std::array<assembly_attachment, 9>{
	    {{{"attach_h2_gp25_vm", "tag_gp25", "tag_gp25"}, attachment_role::launcher, 9},
	     {{"attach_h2_shotgun_vm", "tag_shotgun", "tag_shotgun"}, attachment_role::shotgun, 5},
	     {{"attach_h2_ak47_cover_vm", "tag_cover", "tag_cover"}, attachment_role::cover, 1},
	     {{"attach_h2_ak47_cover_vm_arctic", "tag_cover", "tag_cover"}, attachment_role::cover, 1},
	     {{"attach_h2_ak47_cover_vm_digital", "tag_cover", "tag_cover"}, attachment_role::cover, 1},
	     {{"attach_h2_ak47_cover_vm_desert", "tag_cover", "tag_cover"}, attachment_role::cover, 1},
	     {{"attach_h2_silencer_02_vm", "tag_silencer", "tag_silencer", "tag_flash_silenced"},
	      attachment_role::silencer,
	      2},
	     {{"attach_h2_silencer_03_vm", "tag_silencer", "tag_silencer", "tag_flash_silenced"},
	      attachment_role::silencer,
	      2},
	     {{"attach_h2_ak47_cover_vm_woodland", "tag_cover", "tag_cover"}, attachment_role::cover, 1}}});
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
		if (skin < 0 || receiver.count != 25)
			return {nullptr, "AK receiver contract rejected"};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "AK attachment topology or cardinality rejected"};
		const auto gp25 = bound.counts[static_cast<size_t>(attachment_role::launcher)];
		const auto shotgun = bound.counts[static_cast<size_t>(attachment_role::shotgun)];
		if (gp25 + shotgun > 1)
			return {nullptr, "AK underbarrels are mutually exclusive"};
		return {&assemblies[skin * 3 + (gp25      ? 1
		                                : shotgun ? 2
		                                          : 0)],
		        "AK rifle / support attachment matched",
		        {},
		        bound.muzzle};
	}
}
