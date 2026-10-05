#pragma once
#include "magazine_fill.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "magazine_grasps.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::aug
{
	inline bool native_family(std::string_view name) noexcept
	{
		return native_weapon_family(name, "aug");
	}
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "aug",
		                     .native_name = "aug",
		                     .ammunition = reload_rules,
		                     .interaction = reload_interaction,
		                     .magazine_bone = "tag_clip",
		                     .slide_bone = "j_reload",
		                     .bullets_bone = "j_bullets",
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
		                     .rigid_magazine_source = "h2_viewmodel_steyr_base_arctic",
		                     .receiver_parented_bullets = false,
		                     .bolt = nullptr,
		                     .handle_fold = nullptr,
		                     .sound_override = nullptr,
		                     .handle_catch = &handle_catch};
		    p.magazine_grasps = magazine_grasps;
		    p.magazine_selection = magazine_grasp_policy::body_palm;
		    p.magazine_default_pose = 1;
		    p.magazine_tracking = magazine_tracking_frame::controller;
		    p.interaction = physical_reload::with_box_magazine_well(p.interaction);
		    p.interaction.magazine_pose_count = static_cast<std::uint8_t>(magazine_grasps.size());
		    p.magazine_fills = magazine_fills;
		    return p;
	    }(),
	    {
	        .cycle_notetrack = "weap_styaug_chamber_plr",
	        .retain_close = false,
	        .split_removal = true,
	    });
	// Captured plain/arctic receivers and foregrips share every bind transform.
	inline const reload_profile plain = []
	{
		auto p = physical;
		p.id = "aug_plain";
		p.rigid_magazine_source = "h2_viewmodel_steyr_base";
		return p;
	}();
	inline const std::array<const reload_profile*, 2> skins{&physical, &plain};
	static_assert(action_grips.size() == reload_interaction.slide_pose_count);
}
