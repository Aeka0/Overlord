#pragma once
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "magazine_contacts.hpp"
#include "reload_interaction.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::miniuzi
{
	// Native pullout_first frames 20..22, with closed/rear endpoint clamps.
	// The animation's brief bolt anticipation before handle motion is omitted;
	// manual geometry follows actual hand travel. Idle supplies the sear stop.
	inline constexpr std::array<bolt_travel_sample, 5> bolt_curve{{{0, 0},
	                                                               {.02189896f, .02625733f},
	                                                               {.06272637f, .04427821f},
	                                                               {.08463376f, .05251632f},
	                                                               {.085f, .05251632f}}};
	inline constexpr charging_handle_bolt internal_bolt{"j_open_reload", bolt_rest, bolt_curve, .05251312f};
	inline const reload_profile physical =
	    with_split_sounds(reload_profile{.id = "miniuzi",
	                                     .native_name = native_name,
	                                     .ammunition = reload_rules,
	                                     .interaction = reload_interaction,
	                                     .magazine_bone = "tag_clip",
	                                     .slide_bone = "j_bolt",
	                                     .bullets_bone = "tag_bullet",
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
	                                     .rigid_magazine_source = "h2_viewmodel_miniuzi_base",
	                                     .receiver_parented_bullets = false,
	                                     .bolt = &internal_bolt,
	                                     .handle_fold = nullptr,
	                                     .sound_override = sound_override},
	                      {
	                          .cycle_notetrack = "weap_miniuzi_chamber_plr",
	                          .retain_close = false,
	                          .split_removal = false,
	                      });
	static_assert(action_grips.size() == reload_interaction.slide_pose_count);
}
