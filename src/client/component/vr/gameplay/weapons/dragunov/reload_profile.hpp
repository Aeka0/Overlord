#pragma once
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::dragunov
{
	// Ten-round detachable magazine; no button-operated magazine or bolt release.
	inline constexpr mechanics::rules reload_rules{10,mechanics::magazine_release::physical_pull,true,false,true};
	inline constexpr auto manual_magazine=physical_reload::rocking_magazine();
	// Match the AK side boundary: the raw wrist must stay outside the right
	// receiver wall, so assisted hook contact cannot reach through the magazine.
	inline constexpr part_capture_halfspace handle_capture{{0,1,0},action_grab_high[1]};
	inline constexpr physical_reload::profile reload_interaction{
		.18f,.06f,action_stroke_m,action_stroke_m-.006f,action_stroke_m-.003f,
		.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,2,
		physical_reload::action_motion::reciprocating_slide,&manual_magazine};
	inline const char* sound_key(mechanics::effect kind)noexcept
	{
		switch(kind)
		{
		case mechanics::effect::magazine_out:
		case mechanics::effect::magazine_take:return "weap_dragunovsniper_clipout_plr";
		case mechanics::effect::magazine_in:return "weap_dragunovsniper_clipin_plr";
		case mechanics::effect::action_close:return "weap_dragunovsniper_chamber_plr";
		default:return nullptr;
		}
	}
	inline const reload_profile physical=[] {reload_profile p{
		.id="dragunov",
		.native_name="dragunov",
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
		.slide_grab_low=action_grab_low,
		.slide_grab_high=action_grab_high,
		.magazine_fingers=magazine_fingers,
		.slide_grips=action_grips,
		.sound_key=sound_key,
		.magazine_contacts=&contacts,
		.rigid_magazine_source="h2_viewmodel_dragunov_base"
		};
		p.slide_capture=&handle_capture;return p;
	}();
	inline const reload_profile arctic_physical=[] {
		auto p=physical;p.id="dragunov_arctic";p.native_name="dragunov_arctic";
		p.rigid_magazine_source="h2_viewmodel_dragunov_base_arctic";return p;
	}();
	inline const reload_profile woodland_physical=[] {
		auto p=physical;p.id="dragunov_woodland";p.native_name="dragunov_woodland";
		p.rigid_magazine_source="h2_viewmodel_dragunov_base_woodland";return p;
	}();
}
