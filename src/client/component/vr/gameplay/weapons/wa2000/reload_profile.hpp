#pragma once
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::wa2000
{
	inline bool native_family(std::string_view name) noexcept
	{
		return native_weapon_family(name, "wa2000");
	}
	// Native H2 wa2000 definition carries ten rounds (including campaign variants).
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 10,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = true,
	    .release_control = true,
	    .plus_one = true,
	};
	// Manual straight-down extraction; spare-magazine striking is not enabled.
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
	    .grab_radius = physical_reload::defaults::magazine_grab_radius_m,
	    .pull_travel = physical_reload::defaults::magazine_pull_travel_m,
	    .pull_lateral_limit = physical_reload::defaults::magazine_pull_lateral_limit_m,
	    .pull_axis = {0, 0, -1},
	    .latch_radius = physical_reload::defaults::magazine_latch_radius_m,
	    .latch_rearm_radius = physical_reload::defaults::magazine_latch_rearm_radius_m,
	    .latch_min_speed = physical_reload::defaults::magazine_latch_min_speed_mps,
	    .latch_min_travel = physical_reload::defaults::magazine_latch_min_travel_m,
	    .latch_direction = {1, 0, 0},
	    .spare_strike = false,
	};
	inline constexpr physical_reload::profile reload_interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = part_grip_capture::radius_m,
	    .slide_stroke = action_stroke_m,
	    .locked_travel = 0.f,
	    .full_stroke = action_stroke_m * .95f,
	    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	    .well_radius = physical_reload::defaults::well_radius_m,
	    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
	    .insertion_cosine = physical_reload::defaults::insertion_cosine,
	    .max_contact_step = physical_reload::defaults::max_contact_step_m,
	    .slide_axis = physical_reload::defaults::rearward_axis,
	    .well_capture_below = physical_reload::defaults::well_capture_below_m,
	    .slide_pose_count = static_cast<std::uint8_t>(action_grips.size()),
	    .motion = physical_reload::action_motion::charging_handle,
	    .manual_magazine = &manual_magazine,
	};
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out:
		case magazine_take:
			return "weap_m4carbine_clipout_plr";
		case magazine_in:
			return "weap_m4carbine_clipin_plr";
		case action_close:
			return "weap_ak47_chamber_plr";
		default:
			return nullptr;
		}
	}
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "wa2000",
		                     .native_name = "wa2000",
		                     .ammunition = reload_rules,
		                     .interaction = reload_interaction,
		                     .magazine_bone = "tag_clip",
		                     .slide_bone = "j_reload",
		                     .bullets_bone = "j_ammo",
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
		                     .native_family = native_family,
		                     .magazine_contacts = &contacts,
		                     .rigid_magazine_source = "h2_viewmodel_wa2000_base"};
		    p.receiver_parented_bullets = true;
		    p.bolt = &bolt;
		    return p;
	    }(),
	    {
	        .cycle_notetrack = "weap_ak47_chamber_plr",
	        .retain_close = false,
	        .split_removal = false,
	    });
	inline const std::array<const reload_profile*, 1> skins{&physical};
} // namespace vr::gameplay::weapons::wa2000
