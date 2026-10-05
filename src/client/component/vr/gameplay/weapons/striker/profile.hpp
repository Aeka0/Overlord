#pragma once
#include "poses.hpp"
#include "reload_poses.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"
namespace vr::gameplay::weapons::striker
{
	inline constexpr std::array<std::string_view,2> names{"striker","striker_reflex"};
	inline constexpr std::array<std::string_view,2> woodland_names{"striker_woodland","striker_woodland_reflex"};
	inline sound_reference sound(tube::effect e)noexcept
	{
		switch(e){case tube::effect::draw:return {"weap_striker_lift_plr"};
		case tube::effect::load_port:return {"weap_striker_clipin_plr"};default:return {};}
	}
	inline const tube_profile feed=[] {
		tube_profile p{};p.id="striker";p.receiver="h2_viewmodel_striker_base";p.native_variants=names;
		p.ammunition={12,tube::action_drive::automatic,tube::feed_layout::fixed_drum};
		// Shared supply and shell contact tuning. Rack acquisition is disabled by
		// the feed policy; no dummy handle/bone is exposed to either hand.
		p.interaction={{.18f,part_grip_capture::radius_m,.07f,0,.0665f,
			.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,1},.05f,.05f,.35f,{.015f,.04f}};
		p.lifter_rest=cover_rest;p.lifter_loaded=cover_open;p.shell_in_wrist=shell_in_wrist;
		p.shell_center=shell_center;p.port_center=port_center;p.tube_center=port_center;
		p.port_forward=port_forward;p.tube_forward=port_forward;p.shell_fingers=shell_fingers;p.sound_key=sound;
		p.bolt_bone={};p.lifter_bone="j_ammo_cover";p.shell_bone="j_ammo";p.receiver_bones=20;
		p.drum_bone="j_clip";p.drum_rest=drum_rest;return p;
	}();
	inline bool suppress_equip(std::string_view name)noexcept{return base_equip_action(name,"h2_wpn_sho_striker_");}
	inline const tube_profile woodland_feed=[] {
		auto p=feed;p.id="striker_woodland";p.receiver="h2_viewmodel_striker_base_woodland";p.native_variants=woodland_names;return p;
	}();
	inline const profile base=[] {
		profile p{"striker",feed.receiver,"foregrip",aim_rule::two_hand,wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,nullptr,{part_visibility::rigid_groups}};
		p.tube=&feed;return p;
	}();
	inline const profile woodland=[] {auto p=base;p.receiver=woodland_feed.receiver;p.tube=&woodland_feed;return p;}();
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept
	{
		const auto* p=receiver.name==base.receiver ? &base : receiver.name==woodland.receiver ? &woodland : nullptr;
		if(!p || receiver.count!=20)return {};
		const auto bound=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);
		if(!bound.valid)return {nullptr,"Striker attachment topology rejected"};
		return {p,"Striker fixed drum with automatic single-shell indexing",{},bound.muzzle};
	}
}
