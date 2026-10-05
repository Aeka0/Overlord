#pragma once
#include "reload_profile.hpp"
#include "../../weapon_actions.hpp"
#include "../attachments/rifle.hpp"

namespace vr::gameplay::weapons::vector
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_smg_kriss_"); }
	inline const std::array<profile,2> assemblies=[] {
		std::array<profile,2> out;
		for (size_t skin=0;skin<skins.size();++skin)
			out[skin]={"vector",skins[skin]->rigid_magazine_source,"foregrip",aim_rule::two_hand,wrists,1,
				.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,skins[skin],{part_visibility::rigid_groups}};
		return out;
	}();
	inline int receiver_skin(std::string_view name) noexcept
	{
		for (size_t i=0;i<skins.size();++i) if (name==skins[i]->rigid_magazine_source) return static_cast<int>(i);
		return -1;
	}
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones) noexcept
	{
		const int skin=receiver_skin(receiver.name);
		if (skin<0 || receiver.count!=18) return {nullptr,"Vector receiver contract rejected"};
		const auto bound=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);
		if (!bound.valid) return {nullptr,"Vector attachment topology or cardinality rejected"};
		return {&assemblies[skin],"Vector integral foregrip / receiver skin matched",{},bound.muzzle};
	}
}
