#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::fal
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		return base_equip_action(animation,"h2_wpn_asl_fn_fal_") ||
			base_equip_action(animation,"h2_wpn_asl_fn_fal_shotgun_");
	}
	inline constexpr auto attached_fingers=[] {
		auto out=idle_fingers;
		for (auto& finger:out) for (const auto& support:shotgun_fingers)
			if (finger.name==support.name) finger.rotation=support.rotation;
		return out;
	}();
	inline const profile bare{
		"fal","h2_viewmodel_fn_fal_base","bare",aim_rule::two_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
	inline const profile shotgun=[] {
		auto p=bare;p.variant="shotgun";p.wrists[0]=shotgun_support;p.fingers=attached_fingers;
		p.free_hand_reference=&wrists;return p;
	}();
	inline constexpr auto attachments=rifle_attachments::with_common(std::array<assembly_attachment,1>{{
		{{"attach_h2_shotgun_vm","tag_shotgun","tag_shotgun"},attachment_role::shotgun,5}
	}});
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.name!=bare.receiver || receiver.count!=21) return {nullptr,"FAL receiver contract rejected"};
		const auto bound=bind_attachment_set(attachments,models,receiver,rig,bones);
		if (!bound.valid) return {nullptr,"FAL attachment topology or cardinality rejected"};
		return {bound.counts[static_cast<size_t>(attachment_role::shotgun)] ? &shotgun : &bare,
			"FAL rifle / support attachment matched",{},bound.muzzle};
	}
}
