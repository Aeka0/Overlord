#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::pp2000
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_pst_pp2000_"); }
	inline const profile base{
		"pp2000","h2_viewmodel_p2000_base","foregrip",aim_rule::two_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.name!=base.receiver || receiver.count!=18) return {nullptr,"PP2000 receiver contract rejected"};
		const auto bound=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);
		if (!bound.valid) return {nullptr,"PP2000 attachment topology or cardinality rejected"};
		return {&base,"PP2000 integral foregrip / primary feed matched",{},bound.muzzle};
	}
}
