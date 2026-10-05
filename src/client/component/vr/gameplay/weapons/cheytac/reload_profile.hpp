#pragma once
#include "../../magazine_grasp_profile.hpp"
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::cheytac
{
	inline bool native_family(std::string_view name) noexcept
	{
		return native_weapon_family(name, "cheytac");
	}
	inline constexpr mechanics::rules reload_rules{
	    .magazine_capacity = 5,
	    .release = mechanics::magazine_release::physical_pull,
	    .last_round_lock = false,
	    .release_control = false,
	    .plus_one = true,
	    .feed = mechanics::feed_type::manual_bolt,
	};
	// Native rechamber j_bolt: approximately 52 degrees of lift, 23.028 cm travel.
	inline constexpr rotating_bolt::profile bolt_motion{
	    .pivot = {.10912259f, .00006835f, .10197534f},
	    .radians = -.914f,
	    .stroke = action_stroke_m,
	    .actor = hand::none,
	    .release_assist = true,
	};
	// Existing downward extraction, plus the whole spare body striking the rear paddle forward.
	inline constexpr auto manual_magazine = physical_reload::rocking_magazine();
	inline constexpr physical_reload::profile reload_interaction{
	    .waist_radius = physical_reload::defaults::waist_radius_m,
	    .slide_radius = part_grip_capture::radius_m,
	    .slide_stroke = action_stroke_m,
	    .locked_travel = 0,
	    .full_stroke = action_stroke_m * .95f,
	    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	    .well_radius = physical_reload::defaults::well_radius_m,
	    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
	    .insertion_cosine = physical_reload::defaults::insertion_cosine,
	    .max_contact_step = physical_reload::defaults::max_contact_step_m,
	    .slide_axis = physical_reload::defaults::rearward_axis,
	    .well_capture_below = physical_reload::defaults::well_capture_below_m,
	    .slide_pose_count = 2,
	    .motion = physical_reload::action_motion::rotating_bolt,
	    .manual_magazine = &manual_magazine,
	    .manual_catch = nullptr,
	    .manual_bolt = &bolt_motion,
	};
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out:
		case magazine_take:
			return "weap_cheytac_clipout_plr";
		case magazine_in:
			return "weap_cheytac_clipin_plr";
		case bolt_unlock:
			return "weap_cheytac_bolt_unlock_plr";
		case case_eject:
		case live_eject:
			return "weap_cheytac_bolt_open_plr";
		case action_close:
			return "weap_cheytac_bolt_close_plr";
		case bolt_lock:
			return "weap_cheytac_bolt_lock_plr";
		default:
			return nullptr;
		}
	}
	// Native idle and rechamber frames 17/20, centimetres converted once to model units.
	inline constexpr std::array<hands::anchor, 3> feeding_path{
	    {{{17.849387f / 2.54f, .011326f / 2.54f, 8.455921f / 2.54f}, {0, 0, 0, 1}},
	     {{17.396844f / 2.54f, .070768f / 2.54f, 9.817501f / 2.54f}, {0, 0, 0, 1}},
	     {{39.814776f / 2.54f, -.146145f / 2.54f, 10.084354f / 2.54f}, {0, 0, 0, 1}}}};
	inline const reload_profile physical = []
	{
		reload_profile p{.id = "cheytac",
		                 .native_name = "cheytac",
		                 .ammunition = reload_rules,
		                 .interaction = reload_interaction,
		                 .magazine_bone = "tag_clip",
		                 .slide_bone = "j_bolt",
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
		                 .rigid_magazine_source = "h2_viewmodel_cheytac_base"};
		p.feeding_path = &feeding_path;
		return with_controller_magazine(p);
	}();
	// Live desert receiver has identical bone hierarchy and bind transforms.
	// Keep its actual mesh source so magazine subsets retain desert materials.
	inline const reload_profile desert = []
	{
		auto p = physical;
		p.id = "cheytac_desert";
		p.rigid_magazine_source = "h2_viewmodel_cheytac_base_desert";
		return p;
	}();
} // namespace vr::gameplay::weapons::cheytac
