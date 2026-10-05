#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/precision.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::dragunov
{
	inline bool suppress_equip(std::string_view name)noexcept
	{return base_equip_action(name,"h2_wpn_sni_dragunov_");}
	inline const profile base{
		"dragunov","h2_viewmodel_dragunov_base","handguard",aim_rule::two_hand,wrists,1,
		.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
	// Arctic receiver/scope share the captured base hierarchy and bind poses.
	inline const profile arctic=[] {auto p=base;p.receiver=arctic_physical.rigid_magazine_source;p.reload=&arctic_physical;return p;}();
	inline const profile woodland=[] {auto p=base;p.receiver=woodland_physical.rigid_magazine_source;p.reload=&woodland_physical;return p;}();
	inline constexpr auto attachments=rifle_attachments::with_common(std::array<assembly_attachment,3>{{
		{{"attach_h2_dragunov_scope_vm","tag_sight_on","tag_sight_on"},attachment_role::optic,4},
		{{"attach_h2_dragunov_scope_vm_arctic","tag_sight_on","tag_sight_on"},attachment_role::optic,4},
		{{"attach_h2_dragunov_scope_vm_woodland","tag_sight_on","tag_sight_on"},attachment_role::optic,4}}});
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept
	{
		const auto* p=receiver.name==base.receiver ? &base : receiver.name==arctic.receiver ? &arctic : receiver.name==woodland.receiver ? &woodland : nullptr;
		if(!p || receiver.count!=19)return {nullptr,"Dragunov receiver topology rejected"};
		const auto bound=bind_attachment_set(attachments,models,receiver,rig,bones);
		if(!bound.valid)return {nullptr,"Dragunov attachment topology rejected"};
		return {p,"Dragunov physical magazine and manual bolt release",precision_attachments::physical_scope_mask(models,rig,bones),bound.muzzle};
	}
}
