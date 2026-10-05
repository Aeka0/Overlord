#pragma once
#include "magazine_fill.hpp"
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "reload_poses.hpp"
#include "magazine_grasps.hpp"
#include "magazine_contacts.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::acr
{
	// Candidate family discovery only. Exact native instance/mode/capacity and
	// the complete authored assembly remain mandatory before ammunition writes.
	inline bool native_family(std::string_view name) noexcept
	{
		return native_weapon_family(name, "masada");
	}
	// The single top round precedes the lower stack in independent subsets.
	inline constexpr std::array<std::string_view, 1> lower_rounds{"j_bullets"};
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "acr",
		                     .native_name = "masada",
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
		                     .native_family = native_family,
		                     .additional_bullet_bones = lower_rounds,
		                     .rigid_magazine_source = "h2_viewmodel_magpul_masada_base"};
		    p.magazine_grasps = magazine_grasps;
		    p.magazine_selection = magazine_grasp_policy::body_palm;
		    p.magazine_default_pose = 1;
		    p.magazine_tracking = magazine_tracking_frame::controller;
		    p.magazine_contacts = &contacts;
		    p.interaction = physical_reload::with_box_magazine_well(p.interaction);
		    p.interaction.magazine_pose_count = static_cast<std::uint8_t>(magazine_grasps.size());
		    p.magazine_fills = magazine_fills;
		    return p;
	    }(),
	    {
	        .cycle_notetrack = "weap_masada_chamber_plr",
	        .retain_close = false,
	        .split_removal = false,
	    });
	inline const reload_profile black = []
	{
		auto p = physical;
		p.id = "acr_black";
		p.rigid_magazine_source = "h2_viewmodel_magpul_masada_base_black";
		return p;
	}();
	inline const reload_profile digital = []
	{
		auto p = physical;
		p.id = "acr_digital";
		p.rigid_magazine_source = "h2_viewmodel_magpul_masada_base_digital";
		return p;
	}();
	// Live snow-camouflage receiver has the same 19-bone bind as the base.
	inline const reload_profile arctic = []
	{
		auto p = physical;
		p.id = "acr_arctic";
		p.rigid_magazine_source = "h2_viewmodel_magpul_masada_base_arctic";
		return p;
	}();
	inline const std::array<const reload_profile*, 4> skins{&physical, &black, &digital, &arctic};
	static_assert(action_grips.size() == reload_interaction.slide_pose_count);
}
