#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::fn2000
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_asl_fn2000_"); }
	inline const std::array<profile,skins.size()> assemblies=[] {
		std::array<profile,skins.size()> out;
		for(size_t i=0;i<skins.size();++i)
			out[i]={"fn2000",skins[i]->rigid_magazine_source,"support",aim_rule::two_hand,wrists,1,
				.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,skins[i],{part_visibility::rigid_groups}};
		return out;
	}();
	inline int receiver_skin(std::string_view name) noexcept
	{
		for(size_t i=0;i<skins.size();++i) if(name==skins[i]->rigid_magazine_source) return static_cast<int>(i);
		return -1;
	}
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones) noexcept
	{
		const int skin=receiver_skin(receiver.name);
		if(skin<0 || receiver.count!=21) return {nullptr,"FN2000 receiver contract rejected"};
		const auto bound=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);
		if(!bound.valid) return {nullptr,"FN2000 attachment topology or cardinality rejected"};
		return {&assemblies[skin],"FN2000 primary magazine feed matched",{},bound.muzzle};
	}
}
