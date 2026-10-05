#pragma once
#include "reload_poses.hpp"
#include "bolt_partition.hpp"
#include "magazine_grasps.hpp"
#include "../../families/ar.hpp"

namespace vr::gameplay::weapons::m16
{
	inline bool native_family(std::string_view name) noexcept
	{
		return native_weapon_family(name, "m16");
	}
	inline const char* sound_key(mechanics::effect kind) noexcept
	{
		// Existing WeaponDef dump maps these exact M16 notetrack keys.
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out:
			return "weap_m16_clipout_plr";
		case magazine_in:
			return "weap_m16_clipin_plr";
		case action_rear:
			return "weap_m16_first_chamber_plr";
		case action_close:
			return "weap_m16_chamber_close_plr";
		default:
			return nullptr;
		}
	}
	// Receiver SHA-256 58be751a7b9f437110a5ac2176cd92fe12702cb195af7b8a5d463de700a5cfeb.
	// Upper j_bolt_catch paddle on the left receiver, mesh bounds in cm / 2.54.
	inline constexpr physical_reload::receiver_bolt_release bolt_release{
	    .centre = {3.52376484f, .80828852f, 3.49133691f}, .visual_bone = "j_bolt_catch"};
	inline constexpr auto interaction = families::ar::charging_handle({.stroke_m = handle_stroke_m,
	                                                                   .full_stroke_m = handle_stroke_m * .9f,
	                                                                   .receiver_release = &bolt_release});
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "m16",
		                     .native_name = "m16",
		                     .ammunition = families::ar::reload_rules,
		                     .interaction = interaction,
		                     .magazine_bone = "tag_clip",
		                     .slide_bone = "j_reload",
		                     .bullets_bone = "j_bullets",
		                     .magazine_rest = magazine_rest,
		                     .slide_rest = handle_rest,
		                     .rigid_in_magazine = {},
		                     .magazine_in_wrist = magazine_in_wrist,
		                     .magazine_top = magazine_top,
		                     .well = magazine_well,
		                     .slide_grab_low = handle_grab_low,
		                     .slide_grab_high = handle_grab_high,
		                     .magazine_fingers = magazine_fingers,
		                     .slide_grips = handle_grips,
		                     .sound_key = sound_key,
		                     .native_family = native_family,
		                     .magazine_contacts = &contacts,
		                     .rigid_magazine_source = "h2_viewmodel_m16_base"};
		    p.bolt_partition = &bolt_base;
		    p.magazine_grasps = magazine_grasps;
		    p.magazine_selection = magazine_grasp_policy::body_palm;
		    p.magazine_default_pose = 1;
		    p.magazine_tracking = magazine_tracking_frame::controller;
		    p.interaction = physical_reload::with_box_magazine_well(p.interaction);
		    p.interaction.magazine_pose_count = static_cast<std::uint8_t>(magazine_grasps.size());
		    return p;
	    }(),
	    {
	        .cycle_notetrack = "weap_m16_first_chamber_plr",
	        .retain_close = false,
	        .split_removal = false,
	    });
	static_assert(handle_grips.size() == interaction.slide_pose_count);
}
