#pragma once
#include "poses.hpp"
#include "grenadier_poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"

namespace vr::gameplay::weapons::m4
{
	// Receiver mechanics and rear hand are shared across every admitted variant.
	inline bool suppress_equip(std::string_view) noexcept { return false; }
	inline const profile foregrip{
		"m4", "h2_viewmodel_m4_base", "foregrip", aim_rule::two_hand, wrists, 1,
		.10f, .22f, .10f, idle_fingers, {}, suppress_equip,&physical,{part_visibility::skinned_groups}};
	inline constexpr auto grenadier_fingers=[] {
		auto result=idle_fingers;
		for (auto& finger:result) for (const auto& support:launcher_support_fingers)
			if (finger.name==support.name) finger.rotation=support.rotation;
		return result;
	}();
	inline const profile grenadier=[] {
		auto result=foregrip; result.variant="grenadier";
		result.wrists[0]=launcher_support; result.fingers=grenadier_fingers;
		result.free_hand_reference=&wrists;
		return result;
	}();
	inline const profile arctic_grenadier=[] {
		auto result=grenadier; result.receiver=arctic_physical.skinned_receiver;
		result.reload=&arctic_physical; return result;
	}();
	using weapons::attachment_role;
	using attachment=assembly_attachment;
	// Exact exported model aliases; camouflage changes material/mesh identity,
	// not the reviewed support pose. Parent/root checks remain mandatory.
	inline constexpr auto attachments=rifle_attachments::with_common(std::array<assembly_attachment,4>{{
		{{"attach_h2_mp5k_foregrip_vm","tag_foregrip","tag_foregrip"},attachment_role::foregrip,1},
		{{"attach_h2_m203_vm","tag_m203","tag_m203"},attachment_role::launcher,7},
		{{"attach_h2_m4_cover_vm","tag_cover","tag_cover"},attachment_role::cover,1},
		{{"attach_h2_m4_cover_vm_icon","tag_cover","tag_cover"},attachment_role::cover,1},
	}});
	inline profile_match select(std::span<const hands::model_definition> models,
		const hands::model_definition& receiver,const hands::rig& rig,
		std::span<const hands::bone_definition> bones) noexcept
	{
		const bool arctic=receiver.name==arctic_grenadier.receiver;
		if ((!arctic && receiver.name!=foregrip.receiver) || receiver.count!=24 || bones.size()!=static_cast<size_t>(rig.count))
			return {nullptr,"M4 receiver/bone contract unavailable"};
		const auto bound=bind_attachment_set(attachments,models,receiver,rig,bones);
		if (!bound.valid) return {nullptr,"M4 attachment topology or cardinality rejected"};
		const auto foregrips=bound.counts[static_cast<size_t>(attachment_role::foregrip)];
		const auto launchers=bound.counts[static_cast<size_t>(attachment_role::launcher)];
		if (foregrips+launchers!=1) return {nullptr,"M4 requires exactly one foregrip or M203"};
		if (arctic) return launchers ? profile_match{&arctic_grenadier,"M4 arctic M203 assembly matched",{},bound.muzzle} :
			profile_match{nullptr,"M4 arctic support assembly not reviewed"};
		return {launchers ? &grenadier : &foregrip,"M4 shared receiver / underbarrel grip matched",{},bound.muzzle};
	}
}
