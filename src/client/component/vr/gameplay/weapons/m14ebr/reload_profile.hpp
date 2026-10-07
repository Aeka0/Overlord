#pragma once
#include "magazine_fill.hpp"
#include "../../magazine_grasp_profile.hpp"
#include "reload_poses.hpp"
#include "magazine_grasps.hpp"

namespace vr::gameplay::weapons::m14ebr
{
	inline bool native_family(std::string_view name) noexcept
	{
		// Contingency's thermal asset uses this distinct native name with the
		// same H2 receiver, ten-round feed and reviewed thermal-scope assembly.
		return native_weapon_family(name, "m14") || native_weapon_family(name, "m21") ||
		       name == "m14ebr_thermal";
	}
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 10,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = true,
	    .release_control = false,
	    .plus_one = true,
	};
	// Physical extraction and AK-style spare-magazine impact on the rear latch.
	inline constexpr auto manual_magazine = physical_reload::rocking_magazine();
	// Keep the capture wrist outside the real right-side tab, clear of the
	// magazine. This uses the shared AK boundary for either physical hand.
	inline constexpr part_capture_halfspace handle_capture{{0, 1, 0}, action_grab_high[1]};
	// Native idle j_bullet is an 8.57 cm cartridge above the magazine lips.
	inline constexpr hands::anchor chamber_round{{8.46773921f, .103706f, 4.86337425f}, {0, 0, 0, 1}};
	inline constexpr physical_reload::profile reload_interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = .06f,
	    .slide_stroke = action_stroke_m,
	    .locked_travel = .08454f,
	    .full_stroke = action_stroke_m * .95f,
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
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out:
		case magazine_take:
			return "weap_m14sniper_clipout_plr";
		case magazine_in:
			return "weap_m14sniper_clipin_plr";
		case action_close:
			return "weap_m14sniper_chamber_close_plr";
		default:
			return nullptr;
		}
	}
	inline const reload_profile physical = with_magazine_population(
	    with_split_sounds(
	        []
	        {
		        reload_profile p{.id = "m14ebr",
		                         .native_name = "m14",
		                         .ammunition = reload_rules,
		                         .interaction = reload_interaction,
		                         .magazine_bone = "tag_clip",
		                         .slide_bone = "j_reload",
		                         .bullets_bone = "j_bullet",
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
		                         .rigid_magazine_source = "h2_viewmodel_m14ebr_base"};
		        p.chamber_round = &chamber_round;
		        p.slide_capture = &handle_capture;
		        p.magazine_grasps = magazine_grasps;
		        return with_controller_magazine(p);
	        }(),
	        {
	            .cycle_notetrack = "weap_m14sniper_chamber_close_plr",
	            .retain_close = false,
	            .split_removal = false,
	        }),
	    magazine_fills);
	inline const reload_profile arctic = []
	{
		auto p = physical;
		p.id = "m14ebr_arctic";
		p.rigid_magazine_source = "h2_viewmodel_m14ebr_base_arctic";
		return p;
	}();
	inline const std::array<const reload_profile*, 2> skins{&physical, &arctic};
} // namespace vr::gameplay::weapons::m14ebr
