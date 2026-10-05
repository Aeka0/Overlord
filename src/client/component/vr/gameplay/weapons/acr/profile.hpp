#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../../weapon_actions.hpp"
#include "../attachments/rifle.hpp"

namespace vr::gameplay::weapons::acr
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{ return base_equip_action(animation,"h2_wpn_asl_masada_") || base_equip_action(animation,"h2_wpn_asl_masada_gl_") ||
		base_equip_action(animation,"h2_wpn_asl_masada_hb_open_") || base_equip_action(animation,"h2_wpn_asl_masada_hb_close_"); }
	inline constexpr auto grenadier_fingers=[] {
		// Preserve native rear fingers; also author the support palm and webbing
		// so equip suppression cannot leave them in the bare-handguard pose.
		auto out=idle_fingers;
		for (auto& finger:out) for (const auto& support:launcher_fingers) if (finger.name==support.name) finger.rotation=support.rotation;
		return out;
	}();
	inline const auto assemblies=[] {
		std::array<profile,skins.size()*2> out;
		for (size_t skin=0;skin<skins.size();++skin)
		{
			profile bare{"acr",skins[skin]->rigid_magazine_source,"bare",aim_rule::two_hand,wrists,1,
				.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,skins[skin],{part_visibility::rigid_groups}};
			out[skin*2]=bare;
			auto gl=bare; gl.variant="grenadier"; gl.wrists[0]=launcher_support; gl.fingers=grenadier_fingers; gl.free_hand_reference=&wrists;
			out[skin*2+1]=gl;
		}
		return out;
	}();
	inline constexpr std::array<assembly_attachment,2> launchers{{
		{{"attach_h2_m203_vm","tag_m203","tag_m203"},attachment_role::launcher,7},
		// Live digital ACR: same seven-bone M203 root contract, distinct asset.
		{{"attach_h2_m203_vm_digital","tag_m203","tag_m203"},attachment_role::launcher,7}
	}};
	inline constexpr auto attachments=rifle_attachments::with_common(launchers);
	inline int receiver_skin(std::string_view name) noexcept
	{
		for (size_t i=0;i<skins.size();++i) if (name==skins[i]->rigid_magazine_source) return static_cast<int>(i);
		return -1;
	}
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones) noexcept
	{
		const int skin=receiver_skin(receiver.name);
		if (skin<0 || receiver.count!=19) return {nullptr,"ACR receiver contract rejected"};
		const auto bound=bind_attachment_set(attachments,models,receiver,rig,bones);
		if (!bound.valid) return {nullptr,"ACR attachment topology or cardinality rejected"};
		const auto launcher=bound.counts[static_cast<size_t>(attachment_role::launcher)];
		return {&assemblies[skin*2+launcher],"ACR rifle / support attachment matched",{},bound.muzzle};
	}
}
