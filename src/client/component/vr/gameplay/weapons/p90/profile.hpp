#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"
namespace vr::gameplay::weapons::p90
{
	inline bool suppress_equip(std::string_view name)noexcept{return base_equip_action(name,"h2_wpn_smg_p90_");}
	inline const std::array<profile,skins.size()> assemblies=[] {
		std::array<profile,skins.size()> out{};
		for(size_t i=0;i<skins.size();++i)out[i]={"p90",skins[i]->rigid_magazine_source,"support",aim_rule::two_hand,
			wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,skins[i],{part_visibility::rigid_groups}};
		return out;
	}();
	inline int receiver_skin(std::string_view name)noexcept
	{for(size_t i=0;i<assemblies.size();++i)if(assemblies[i].receiver==name)return int(i);return -1;}
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept
	{
		const int skin=receiver_skin(receiver.name);if(skin<0 || receiver.count!=37)return {};
		const auto bound=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);
		if(!bound.valid)return {nullptr,"P90 attachment topology rejected"};
		return {&assemblies[skin],"P90 top magazine and bilateral charging handles",{},bound.muzzle};
	}
}
