#pragma once
#include "magazine_fill.hpp"
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "magazine_contacts.hpp"
#include "reload_interaction.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::tmp
{
	// Parent-local native pullout_first frames 10,12..17. Separate channels,
	// not a rigid 1:1 handle/bolt transform. Clamp at the reviewed rear stop.
	inline constexpr std::array<bolt_travel_sample, 8> bolt_curve{{{0, 0},
	                                                               {.00898384f, 0},
	                                                               {.01212242f, .00214216f},
	                                                               {.02138231f, .00606722f},
	                                                               {.03846467f, .01069097f},
	                                                               {.07019420f, .03029163f},
	                                                               {.08729389f, .04364531f},
	                                                               {.088f, .04364531f}}};
	// h2_wpn_pst_mp9_fire j_bolt spans 48.65016 mm. Its firing travel is
	// independent of the shorter manual stroke and the handle's initial slack.
	inline constexpr charging_handle_bolt internal_bolt{
	    "j_bolt", equip_rest[0].local, bolt_curve, .04364531f, .04865016f};
	inline constexpr std::array<std::string_view, 2> extra_rounds{"j_bullet02", "j_bullet03"};
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "tmp",
		                     .native_name = native_name,
		                     .ammunition = reload_rules,
		                     .interaction = reload_interaction,
		                     .magazine_bone = "tag_clip",
		                     .slide_bone = "j_reload",
		                     .bullets_bone = "j_bullet01",
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
		                     .native_family = native_variant,
		                     .magazine_contacts = &contacts,
		                     .additional_bullet_bones = extra_rounds,
		                     .rigid_magazine_source = "h2_viewmodel_mp9_base",
		                     .receiver_parented_bullets = false,
		                     .bolt = &internal_bolt};
		    p.magazine_fills = magazine_fills;
		    return p;
	    }(),
	    {
	        .cycle_notetrack = "weap_mp9_chamber_plr",
	        .retain_close = false,
	        .split_removal = false,
	    });
	static_assert(action_grips.size() == reload_interaction.slide_pose_count);
}
