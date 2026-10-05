#pragma once
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::dragunov
{
	// Ten-round detachable magazine; no button-operated magazine or bolt release.
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 10,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = true,
	    .release_control = false,
	    .plus_one = true,
	};
	inline constexpr auto manual_magazine = physical_reload::rocking_magazine();
	// Match the AK side boundary: the raw wrist must stay outside the right
	// receiver wall, so assisted hook contact cannot reach through the magazine.
	inline constexpr part_capture_halfspace handle_capture{{0, 1, 0}, action_grab_high[1]};
	inline constexpr physical_reload::profile reload_interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = .06f,
	    .slide_stroke = action_stroke_m,
	    .locked_travel = action_stroke_m - .006f,
	    .full_stroke = action_stroke_m - .003f,
	    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	    .well_radius = physical_reload::defaults::well_radius_m,
	    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
	    .insertion_cosine = physical_reload::defaults::insertion_cosine,
	    .max_contact_step = physical_reload::defaults::max_contact_step_m,
	    .slide_axis = physical_reload::defaults::rearward_axis,
	    .well_capture_below = physical_reload::defaults::well_capture_below_m,
	    .slide_pose_count = 2,
	    .motion = physical_reload::action_motion::reciprocating_slide,
	    .manual_magazine = &manual_magazine,
	};
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		switch (kind)
		{
		case mechanics::effect::magazine_out:
		case mechanics::effect::magazine_take:
			return "weap_dragunovsniper_clipout_plr";
		case mechanics::effect::magazine_in:
			return "weap_dragunovsniper_clipin_plr";
		case mechanics::effect::action_close:
			return "weap_dragunovsniper_chamber_plr";
		default:
			return nullptr;
		}
	}
	inline const reload_profile physical = []
	{
		reload_profile p{.id = "dragunov",
		                 .native_name = "dragunov",
		                 .ammunition = reload_rules,
		                 .interaction = reload_interaction,
		                 .magazine_bone = "tag_clip",
		                 .slide_bone = "j_bolt",
		                 .bullets_bone = "tag_bullet_single",
		                 .magazine_rest = magazine_rest,
		                 .slide_rest = action_rest,
		                 .rigid_in_magazine = {},
		                 .magazine_in_wrist = magazine_in_wrist,
		                 .magazine_top = magazine_top,
		                 .well = magazine_well,
		                 .slide_grab_low = action_grab_low,
		                 .slide_grab_high = action_grab_high,
		                 .magazine_fingers = magazine_fingers,
		                 .slide_grips = action_grips,
		                 .sound_key = sound_key,
		                 .magazine_contacts = &contacts,
		                 .rigid_magazine_source = "h2_viewmodel_dragunov_base"};
		p.slide_capture = &handle_capture;
		return p;
	}();
	inline const reload_profile arctic_physical = []
	{
		auto p = physical;
		p.id = "dragunov_arctic";
		p.native_name = "dragunov_arctic";
		p.rigid_magazine_source = "h2_viewmodel_dragunov_base_arctic";
		return p;
	}();
	inline const reload_profile woodland_physical = []
	{
		auto p = physical;
		p.id = "dragunov_woodland";
		p.native_name = "dragunov_woodland";
		p.rigid_magazine_source = "h2_viewmodel_dragunov_base_woodland";
		return p;
	}();
}
