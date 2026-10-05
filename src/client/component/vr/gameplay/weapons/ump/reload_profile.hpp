#pragma once
#include "../../magazine_grasp_profile.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "bolt_partition.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::ump
{
	inline bool native_family(std::string_view name) noexcept
	{
		return native_weapon_family(name, "ump45");
	}
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "ump",
		                     .native_name = "ump45",
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
		                     .rigid_magazine_source = "h2_viewmodel_ump45_base",
		                     .receiver_parented_bullets = false,
		                     .bolt = nullptr,
		                     .handle_fold = nullptr,
		                     .sound_override = nullptr,
		                     .handle_catch = &handle_catch};
		    p.bolt_partition = &bolt_base;
		    return with_controller_magazine(p);
	    }(),
	    {
	        .cycle_notetrack = "weap_ump45_chamber_plr",
	        .retain_close = true,
	        .split_removal = false,
	    });
	inline const reload_profile arctic = []
	{
		auto p = physical;
		p.id = "ump_arctic";
		p.rigid_magazine_source = "h2_viewmodel_ump45_base_arctic";
		p.bolt_partition = &bolt_arctic;
		return p;
	}();
	inline const reload_profile digital = []
	{
		auto p = physical;
		p.id = "ump_digital";
		p.rigid_magazine_source = "h2_viewmodel_ump45_base_digital";
		p.bolt_partition = &bolt_digital;
		return p;
	}();
	inline const std::array<const reload_profile*, 3> skins{&physical, &arctic, &digital};
	static_assert(action_grips.size() == reload_interaction.slide_pose_count);
}
