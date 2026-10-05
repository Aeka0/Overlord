#pragma once
#include "reload_poses.hpp"
#include "magazine_grasps.hpp"

namespace vr::gameplay::weapons::p90
{
	inline bool native_family(std::string_view name)noexcept{return native_weapon_family(name,"p90");}
	inline constexpr mechanics::rules reload_rules{50,mechanics::magazine_release::physical_pull,false,false,true};
	// Acquisition-only rear trim: 1.5 native units (38 mm) toward the muzzle.
	// Preserve bilateral source contacts and bolt stroke while reducing capture
	// along the top-magazine approach.
	inline constexpr hands::vec action_capture_low{action_grab_low[0]+1.5f,action_grab_low[1],action_grab_low[2]};
	// Lift the horizontal magazine out of the top of the receiver. The well's
	// insertion axis points downward; hand extraction is upward in gun space.
	// At the front overlap, distinguish top-magazine vs side-handle wrist facing.
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
		.05f,.05f,.10f,{0,0,1},.025f,.07f,.18f,.012f,{1,0,0},false,true};
	// The 11 cm shared radius still captured the authored magazine wrist after
	// the rear trim. Six cm retains both handle contacts and a 4 cm approach.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,.06f,action_stroke_m,0,action_stroke_m*.95f,
		.18f,.05f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,1,
		physical_reload::action_motion::charging_handle,&manual_magazine};
	inline const char* sound_key(mechanics::effect e)noexcept
	{
		switch(e){case mechanics::effect::magazine_out:case mechanics::effect::magazine_take:return "weap_p90_clipout_plr";
		case mechanics::effect::magazine_in:return "weap_p90_clipin_plr";case mechanics::effect::action_close:return "weap_p90_chamber_plr";default:return nullptr;}
	}
	inline constexpr std::array<std::string_view,1> body_parts{"j_mechanism"};
	inline constexpr std::array<std::string_view,11> contents{
		"j_bulletempty","j_bullet1","j_bullet2","j_bullet3","j_bullet4","j_bullet5","j_bullet6","j_bullet7","j_bullet8","j_bullet9","j_bullet10"};
	inline const reload_profile physical=with_split_sounds([] {
		reload_profile p{
		.id="p90",
		.native_name="p90",
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_bolt",
		.bullets_bone="tag_bullet_single",
		.magazine_rest=magazine_rest,
		.slide_rest=action_rest,
		.rigid_in_magazine={},
		.magazine_in_wrist=magazine_in_wrist,
		.magazine_top=magazine_top,
		.well=magazine_well,
		.slide_grab_low=action_capture_low,
		.slide_grab_high=action_grab_high,
		.magazine_fingers=magazine_fingers,
		.slide_grips=action_grips,
		.sound_key=sound_key,
		.native_family=native_family,
		.magazine_contacts=&contacts,
		.rigid_magazine_source="h2_viewmodel_p90_base"
		};
		p.magazine_body_bones=body_parts;p.magazine_round_variants=contents;p.rounds_per_variant=5;
		p.magazine_grasps=magazine_grasps;p.interaction.magazine_pose_count=static_cast<std::uint8_t>(magazine_grasps.size());return p;
	}(),"weap_p90_chamber_plr",false,false);
	inline const reload_profile arctic=[] {auto p=physical;p.rigid_magazine_source="h2_viewmodel_p90_base_arctic";return p;}();
	inline const std::array<const reload_profile*,2> skins{&physical,&arctic};
}
