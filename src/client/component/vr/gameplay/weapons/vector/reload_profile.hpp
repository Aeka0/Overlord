#pragma once
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "magazine_contacts.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::vector
{
	// Family discovery is provisional; complete scene, exact owned native name,
	// primary mode and base capacity remain mandatory before ammunition writes.
	inline bool native_family(std::string_view name) noexcept
	{
		return native_weapon_family(name, "kriss");
	}
	// reload_empty frame 59 unfolds j_reload by 90 degrees about local +Z.
	inline constexpr charging_handle_fold handle_fold{{0, 0, .70710678f, .70710678f}};
	// H2 manual reload does not animate j_bolt. Retained travel uses fire frame 3
	// (39.402mm); linear manual coupling is authored presentation, not a sampled
	// native handle/bolt curve. Native firing keeps its own bolt animation.
	inline constexpr std::array<bolt_travel_sample, 2> bolt_curve{{{0, 0}, {.061f, .03940248f}}};
	inline constexpr charging_handle_bolt internal_bolt{
	    "j_bolt", equip_rest[0].local, bolt_curve, .03940248f};
	inline const reload_profile physical =
	    with_split_sounds(reload_profile{.id = "vector",
	                                     .native_name = "kriss",
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
	                                     .rigid_magazine_source = "h2_viewmodel_kriss_super_v_base",
	                                     .receiver_parented_bullets = false,
	                                     .bolt = &internal_bolt,
	                                     .handle_fold = &handle_fold},
	                      {
	                          .cycle_notetrack = "weap_kriss_chamber_plr",
	                          .retain_close = false,
	                          .split_removal = true,
	                      });
	inline const reload_profile black = []
	{
		auto p = physical;
		p.id = "vector_black";
		p.rigid_magazine_source = "h2_viewmodel_kriss_super_v_base_black";
		return p;
	}();
	inline const std::array<const reload_profile*, 2> skins{&physical, &black};
	static_assert(action_grips.size() == reload_interaction.slide_pose_count);
}
