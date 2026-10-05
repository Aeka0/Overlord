#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"
#include "../../weapon_attachments.hpp"

namespace vr::gameplay::weapons::tmp
{
	inline constexpr profile base{
		"tmp","h2_viewmodel_mp9_base","foregrip",aim_rule::two_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
	inline constexpr std::array<assembly_attachment,1> attachments{{
		{{"attach_h2_red_dot_sight_vm","tag_red_dot","tag_red_dot"},attachment_role::optic,2}
	}};
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones) noexcept
	{
		if(receiver.name!=base.receiver || receiver.count!=14)return {};
		const auto bound=bind_attachment_set(attachments,models,receiver,rig,bones);
		if(!bound.valid)return {nullptr,"TMP attachment topology rejected"};
		return {&base,"TMP receiver and reviewed reflex optic",{},bound.muzzle};
	}
}
