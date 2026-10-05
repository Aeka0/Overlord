#pragma once
#include "reload_poses.hpp"
#include "../hand_poses/edge_handle.hpp"
#include "../belt_cover_contacts.hpp"
namespace vr::gameplay::weapons::rpd
{
	inline bool native_family(std::string_view n) noexcept
	{
		return native_weapon_family(n, "rpd");
	}
	inline const auto handle_grips = hand_poses::edge_handle::at(handle_contact);
	inline const auto feed_interaction = []
	{
		auto p = belt;
		p.bridge = &optic_bridge;
		p.details = feed_details;
		p.push = &belt_cover_contacts::rpd;
		return p;
	}();
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 100,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = false,
	    .release_control = false,
	    .plus_one = false,
	    .feed = mechanics::feed_type::open_bolt,
	    .manual_catch = false,
	    .belt_fed = true,
	    .belt_bridge = true,
	};
	inline constexpr std::array<bolt_travel_sample, 2> bolt_curve{
	    {{0, 0}, {action_stroke_m, action_stroke_m}}};
	inline constexpr charging_handle_bolt internal_bolt{"j_bolt", bolt_rest, bolt_curve, action_stroke_m};
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
	    .grab_radius = .06f,
	    .pull_travel = .055f,
	    .pull_lateral_limit = .12f,
	    .pull_axis = {0.00000000f, 0.00000000f, -1.00000000f},
	    .latch_radius = physical_reload::defaults::magazine_latch_radius_m,
	    .latch_rearm_radius = .07f,
	    .latch_min_speed = .18f,
	    .latch_min_travel = physical_reload::defaults::magazine_latch_min_travel_m,
	    .latch_direction = {1, 0, 0},
	    .spare_strike = false,
	};
	inline const physical_reload::profile interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = part_grip_capture::radius_m,
	    .slide_stroke = action_stroke_m,
	    .locked_travel = 0,
	    .full_stroke = action_stroke_m * .95f,
	    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	    .well_radius = .065f,
	    .well_contact_depth = .07f,
	    .insertion_cosine = physical_reload::defaults::insertion_cosine,
	    .max_contact_step = physical_reload::defaults::max_contact_step_m,
	    .slide_axis = physical_reload::defaults::rearward_axis,
	    .well_capture_below = .07f,
	    .slide_pose_count = static_cast<std::uint8_t>(handle_grips.size()),
	    .motion = physical_reload::action_motion::charging_handle,
	    .manual_magazine = &manual_magazine,
	    .manual_catch = nullptr,
	    .manual_bolt = nullptr,
	    .belt = &feed_interaction,
	};
	inline const char* sound_key(mechanics::effect e) noexcept
	{
		switch (e)
		{
		case mechanics::effect::magazine_take:
			return "weap_rpd_clipout_plr";
		case mechanics::effect::magazine_in:
			return "weap_rpd_clipin_plr";
		case mechanics::effect::cover_open:
			return "weap_rpd_open_plr";
		case mechanics::effect::cover_close:
			return "weap_rpd_close_plr";
		case mechanics::effect::bridge_open:
			return "h2_wpn_rpd_scope_open_plr";
		case mechanics::effect::bridge_close:
			return "h2_wpn_rpd_scope_close_plr";
		case mechanics::effect::action_rear:
			return "weap_rpd_chamber_plr";
		case mechanics::effect::belt_laid:
			return "weap_rpd_hit_plr";
		default:
			return nullptr;
		}
	}
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "rpd",
		                     .native_name = "rpd",
		                     .ammunition = reload_rules,
		                     .interaction = interaction,
		                     .magazine_bone = "tag_clip",
		                     .slide_bone = "j_bolthandle",
		                     .bullets_bone = "j_bullet1",
		                     .magazine_rest = magazine_rest,
		                     .slide_rest = action_rest,
		                     .rigid_in_magazine = {},
		                     .magazine_in_wrist = magazine_in_wrist,
		                     .magazine_top = magazine_top,
		                     .well = magazine_well,
		                     .slide_grab_low = action_grab_low,
		                     .slide_grab_high = action_grab_high,
		                     .magazine_fingers = magazine_fingers,
		                     .slide_grips = handle_grips,
		                     .sound_key = sound_key,
		                     .native_family = native_family,
		                     .magazine_contacts = &contacts,
		                     .rigid_magazine_source = "h2_viewmodel_rpd_base"};
		    p.bolt = &internal_bolt;
		    p.handle_child_of_bolt = true;
		    p.magazine_body_bones = drum_children;
		    return p;
	    }(),
	    {
	        .cycle_notetrack = "weap_rpd_chamber_plr",
	        .retain_close = false,
	        .split_removal = false,
	    });
	inline const reload_profile digital_physical = []
	{
		auto p = physical;
		p.id = "rpd_digital";
		p.rigid_magazine_source = "h2_viewmodel_rpd_base_digital";
		return p;
	}();
}
