#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::l86
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_lmg_sa80_"); }
	inline const profile base{
		"l86","h2_viewmodel_sa80_lmg_base","support",aim_rule::two_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
	inline constexpr auto attachments=rifle_attachments::with_common(std::array<assembly_attachment,1>{{
		{{"attach_h2_sa80_scope_vm","tag_sa80_scope","tag_sa80_scope"},attachment_role::optic,1}
	}});
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones) noexcept
	{
		if(receiver.name!=base.receiver || receiver.count!=16) return {nullptr,"L86 receiver contract rejected"};
		const auto bound=bind_attachment_set(attachments,models,receiver,rig,bones);
		if(!bound.valid) return {nullptr,"L86 attachment topology or cardinality rejected"};
		return {&base,"L86 primary drum feed matched",{},bound.muzzle};
	}
}
