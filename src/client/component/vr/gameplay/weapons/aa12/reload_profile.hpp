#pragma once
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::aa12
{
	inline bool native_family(std::string_view name) noexcept { return native_weapon_family(name,"aa12"); }
	// MW2CR's AA-12 is closed-bolt: fire and empty_add both return j_reload
	// forward. One accepted shot consumes one shell, regardless of pellet count.
	inline constexpr mechanics::rules reload_rules{
		8,mechanics::magazine_release::physical_pull,false,false,true,mechanics::feed_type::closed_bolt};
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
		.05f,.05f,.10f,{0,0,-1},.025f,.06f,.15f,.012f,{1,0,0},false};
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,action_stroke_m,0,action_stroke_m*.95f,
		.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,1,
		physical_reload::action_motion::reciprocating_slide,&manual_magazine};
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch(kind)
		{
		case magazine_out: case magazine_take: return "weap_aa12_clipout_plr";
		case magazine_in: return "weap_aa12_clipin_plr";
		case action_close: return "weap_aa12_chamber_plr";
		default: return nullptr;
		}
	}
	inline const reload_profile physical=with_split_sounds(reload_profile{
		.id="aa12",
		.native_name="aa12",
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_reload",
		.bullets_bone="j_bullet",
		.magazine_rest=magazine_rest,
		.slide_rest=action_rest,
		.rigid_in_magazine={},
		.magazine_in_wrist=magazine_in_wrist,
		.magazine_top=magazine_top,
		.well=magazine_well,
		.slide_grab_low=action_grab_low,
		.slide_grab_high=action_grab_high,
		.magazine_fingers=magazine_fingers,
		.slide_grips=action_grips,
		.sound_key=sound_key,
		.native_family=native_family,
		.magazine_contacts=&contacts,
		.rigid_magazine_source="h2_viewmodel_aa12_base",
		.action_detail_bone="j_reload_end" // Upper 494-triangle handle piece, child of j_reload.
		},"weap_aa12_chamber_plr",false,false);
	static_assert(action_grips.size()==reload_interaction.slide_pose_count);
}
