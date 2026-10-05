#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::aa12
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_sho_aa12_"); }
	inline const profile base{
		"aa12","h2_viewmodel_aa12_base","support",aim_rule::two_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones) noexcept
	{
		if(receiver.name!=base.receiver || receiver.count!=14)
			return {nullptr,"AA-12 receiver contract rejected"};
		const auto bound=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);
		if(!bound.valid) return {nullptr,"AA-12 attachment topology or cardinality rejected"};
		// The visible top handle belongs to j_reload. Do not admit a renamed or
		// detached child: it would leave the grasp and rendered action separated.
		int action=-1,handle=-1;
		for(int i=receiver.begin;i<receiver.begin+receiver.count;++i)
		{
			if(bones[i].name=="j_reload") { if(action>=0) return {}; action=i; }
			if(bones[i].name=="j_reload_end") { if(handle>=0) return {}; handle=i; }
		}
		if(action<0 || handle<0 || rig.parent[action]!=rig.gun || rig.parent[handle]!=action)
			return {nullptr,"AA-12 top handle hierarchy rejected"};
		return {&base,"AA-12 closed-bolt magazine feed matched",{},bound.muzzle};
	}
}
