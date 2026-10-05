#pragma once
#include "../../physical_reload_configuration.hpp"
#include "../../part_grip_pose.hpp"
#include "../../magazine_manipulation.hpp"
#include "../../handle_catch.hpp"
#include "../../receiver_bolt_release.hpp"

namespace vr::gameplay::weapons::ump
{
	// Pull the magazine or push its rear paddle forward with a spare magazine.
	// This remains independent of the receiver bolt catch.
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
	    .grab_radius = physical_reload::defaults::magazine_grab_radius_m,
	    .pull_travel = physical_reload::defaults::magazine_pull_travel_m,
	    .pull_lateral_limit = physical_reload::defaults::magazine_pull_lateral_limit_m,
	    .pull_axis = {.206228228f, 0, -.978504f},
	    .latch_radius = physical_reload::defaults::magazine_latch_radius_m,
	    .latch_rearm_radius = physical_reload::defaults::magazine_latch_rearm_radius_m,
	    .latch_min_speed = physical_reload::defaults::magazine_latch_min_speed_mps,
	    .latch_min_travel = physical_reload::defaults::magazine_latch_min_travel_m,
	    .latch_direction = {1, 0, 0},
	    .spare_strike = true,
	};
	inline constexpr physical_reload::handle_catch manual_catch{};
	// Native rear stroke; the shared catch handles lift, roll and palm impact.
	// Mesh-reviewed left receiver paddle; gun-local native units.
	// Receiver SHA256 f65f1857e9e152f906e2de28306b4d96ab9b3e64f8c5aa482845fe6ba4dc3cd4
	inline constexpr physical_reload::receiver_bolt_release bolt_release{
	    {5.12596979f, 1.00368496f, 3.51649983f}};
	inline constexpr physical_reload::profile reload_interaction = []
	{
		physical_reload::profile p{
		    .waist_radius = physical_reload::defaults::waist_radius_m,
		    .slide_radius = part_grip_capture::radius_m,
		    .slide_stroke = 0.085f,
		    .locked_travel = 0.f,
		    .full_stroke = 0.079f,
		    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
		    .well_radius = physical_reload::defaults::well_radius_m,
		    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
		    .insertion_cosine = physical_reload::defaults::insertion_cosine,
		    .max_contact_step = physical_reload::defaults::max_contact_step_m,
		    .slide_axis = physical_reload::defaults::rearward_axis,
		    .well_capture_below = physical_reload::defaults::well_capture_below_m,
		    .slide_pose_count = 2,
		    .motion = physical_reload::action_motion::charging_handle,
		    .manual_magazine = &manual_magazine,
		    .manual_catch = &manual_catch,
		};
		p.receiver_release = &bolt_release;
		return p;
	}();
}
