#pragma once
#include "magazine_fill.hpp"
#include "../../magazine_grasp_profile.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "feedback.hpp"
#include "magazine_grasps.hpp"

namespace vr::gameplay::weapons::scar
{
	inline bool native_family(std::string_view name) noexcept
	{
		// Alternate feeds share the receiver/name prefix, never its ammunition.
		return native_weapon_family(name, "scar_h") && !native_weapon_family(name, "scar_h_shotgun_attach") &&
		       !native_weapon_family(name, "scar_h_m203");
	}
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "scar",
		                     .native_name = "scar_h",
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
		                     .rigid_magazine_source = "h2_viewmodel_scar_h_base"};
		    p.magazine_grasps = magazine_grasps;
		    p.magazine_fills = magazine_fills;
		    return with_controller_magazine(p);
	    }(),
	    {
	        .cycle_notetrack = "weap_scar_chamber_plr",
	        .retain_close = false,
	        .split_removal = false,
	    });
	static_assert(action_grips.size() == reload_interaction.slide_pose_count);
}
