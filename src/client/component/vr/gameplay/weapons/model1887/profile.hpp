#pragma once
#include "poses.hpp"
#include "reload_poses.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::model1887
{
	inline constexpr std::array<std::string_view,1> names{"model1887"};
	inline sound_reference sound(tube::effect e)noexcept
	{
		switch(e)
		{
		case tube::effect::draw:return {"weap_m1887_lift_plr"};
		case tube::effect::load_tube:case tube::effect::load_port:return {"weap_m1887_loop_plr"};
		case tube::effect::rack_open:return {"weap_m1887_open_plr"};
		case tube::effect::rack_close:return {"weap_m1887_close_plr"};
		default:return {};
		}
	}
	inline const tube_profile feed=[]
	{
		tube_profile p{};p.id="model1887";p.receiver="h2_viewmodel_model_1887_base";p.native_variants=names;
		// Preserve the captured five-round native budget until a separate plus-one admission.
		p.ammunition={5,tube::action_drive::lever,tube::feed_layout::tube,false};
		// The common feed's travel is metres; lever input itself uses radians.
		p.interaction.rack={.18f,part_grip_capture::radius_m,.10f,.095f,.097f,.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.002f,1};
		// Broad shell contact and 120-degree approach tolerance for controller loading.
		p.interaction.port_radius=.075f;p.interaction.tube_radius=.075f;p.interaction.alignment=-.5f;
		p.interaction.loading={.02f,.025f};
		p.interaction.lever.spin_open=spin_open;
		p.bolt_rest=bolt_rest;p.bolt_open=bolt_rest;p.lifter_rest=p.lifter_loaded=action_rest;
		p.shell_in_wrist=shell_in_wrist;p.shell_center=shell_center;p.port_center=port_center;p.tube_center=tube_center;
		p.port_forward=port_forward;p.tube_forward=tube_forward;p.port_shell=port_shell;
		p.shell_fingers=shell_fingers;p.sound_key=sound;
		// Captured receiver tag_brass, native units. Offset the shell root so
		// feedback's centre composition recovers the exact native marker.
		hands::anchor brass{{6.52105301f,0,4.91204975f},{-.21261597f,-.21261597f,-.67434692f,.67434692f}};
		brass.rotation=hands::normalize(brass.rotation);brass.position=hands::sub(brass.position,hands::rotate(brass.rotation,shell_center));p.ejection_shell=brass;
		p.bolt_bone="j_bolt";p.lifter_bone="j_action";p.shell_bone="j_ammo_01";p.receiver_bones=9;p.lever=&lever_motion;
		return p;
	}();
	inline bool suppress_equip(std::string_view n)noexcept
	{return base_equip_action(n,"h2_wpn_sho_model1887_") || base_equip_action(n,"viewmodel_model1887_akimbo_");}
	inline const profile base=[]
	{
		profile p{"model1887",feed.receiver,"lever",aim_rule::two_hand,wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,nullptr,{part_visibility::rigid_groups}};
		p.tube=&feed;return p;
	}();
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept
	{
		constexpr std::array<std::string_view,9> bones_expected{"j_gun","j_action","j_ammo_01","j_ammo_02","j_ammo_03","j_bolt","tag_brass","tag_flash","j_hammer"};
		if(receiver.count!=9 || receiver.begin<0 || size_t(receiver.begin+9)>bones.size())return {nullptr,"lever receiver topology rejected"};
		for(int i=0;i<9;++i)
			if(bones[receiver.begin+i].name!=bones_expected[i] || (i && rig.parent[receiver.begin+i]!=receiver.begin+(i==8?1:0)))
				return {nullptr,"lever receiver hierarchy rejected"};
		const auto attachments=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);
		return attachments.valid?profile_match{&base,"manual lever tube feed",{},attachments.muzzle}:profile_match{nullptr,"lever attachment topology rejected"};
	}
}
