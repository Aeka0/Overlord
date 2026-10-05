#pragma once
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "reload_poses.hpp"
#include "magazine_grasps.hpp"
#include "magazine_contacts.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::m4
{
	// Discovery is not write authority: native inventory/mode/count validation,
	// an exact M4 receiver and one validated underbarrel must agree first.
	inline bool native_family(std::string_view name) noexcept
	{
		// M203 rifle variants use m4m203[_optic]; m203_m4[_optic] is the
		// separate launcher feed. Both rifle stems still need scene admission.
		return native_weapon_family(name, "m4") || native_weapon_family(name, "m4m203");
	}
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "m4",
		                     .native_name = "m4",
		                     .ammunition = reload_rules,
		                     .interaction = reload_interaction,
		                     .magazine_bone = "tag_clip",
		                     .slide_bone = "j_reload",
		                     .bullets_bone = "j_bullet",
		                     .magazine_model = "h2_weapon_m4_clip",
		                     .magazine_rest = magazine_rest,
		                     .slide_rest = handle_rest,
		                     .rigid_in_magazine = rigid_in_magazine,
		                     .magazine_in_wrist = magazine_in_wrist,
		                     .magazine_top = magazine_top,
		                     .well = magazine_well,
		                     .slide_grab_low = handle_grab_low,
		                     .slide_grab_high = handle_grab_high,
		                     .magazine_fingers = magazine_fingers,
		                     .slide_grips = handle_grips,
		                     .sound_key = sound_key,
		                     .native_family = native_family,
		                     .skinned_receiver = "h2_viewmodel_m4_base"};
		    p.magazine_grasps = magazine_grasps;
		    p.magazine_selection = magazine_grasp_policy::body_palm;
		    p.magazine_default_pose = 1;
		    p.magazine_tracking = magazine_tracking_frame::controller;
		    p.magazine_contacts = &contacts;
		    p.action_detail_bone =
		        "j_reload_trigger"; // Separate 160-triangle charging-handle latch below j_reload.
		    p.interaction = physical_reload::with_box_magazine_well(p.interaction);
		    p.interaction.magazine_pose_count = static_cast<std::uint8_t>(magazine_grasps.size());
		    return p;
	    }(),
	    {
	        .cycle_notetrack = "weap_m4carbine_first_chamber_plr",
	        .retain_close = false,
	        .split_removal = false,
	    });
	// Gulag's captured snow receiver uses the same magazine partition and
	// mechanics, but visibility must prepare this exact loaded XModel.
	inline const reload_profile arctic_physical = []
	{
		auto p = physical;
		p.id = "m4_arctic";
		p.skinned_receiver = "h2_viewmodel_m4_base_arctic";
		return p;
	}();
}
