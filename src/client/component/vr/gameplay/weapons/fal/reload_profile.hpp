#pragma once
#include "magazine_fill.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "reload_poses.hpp"
#include "bolt_partition.hpp"
#include "magazine_grasps.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::fal
{
	inline bool native_family(std::string_view name) noexcept
	{ return native_weapon_family(name,"fal"); }
	inline constexpr std::array<std::string_view,1> animation_magazines{"tag_clip"};
	inline const reload_profile physical=with_split_sounds([] {
		reload_profile p{
		.id="fal",
		.native_name="fal",
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip_02",
		.slide_bone="j_bolt",
		.bullets_bone="j_bullet_02",
		.magazine_rest=magazine_rest,
		.slide_rest=action_rest,
		.rigid_in_magazine={},
		.magazine_in_wrist=magazine_in_wrist,
		.magazine_top=magazine_top,
		.well=magazine_well,
		.slide_grab_low=action_grab_low,
		.slide_grab_high=action_grab_high,
		.magazine_fingers=magazine_fingers,
		.slide_grips=action_grips,
		.sound_key=sound_key,
		.native_family=native_family,
		.magazine_contacts=&contacts,
		.rigid_magazine_source="h2_viewmodel_fn_fal_base"
		};
		p.animation_only_magazines=animation_magazines;
		p.bolt_partition=&bolt_base;
		p.magazine_grasps=magazine_grasps;p.magazine_selection=magazine_grasp_policy::body_palm;p.magazine_default_pose=1;
		p.magazine_tracking=magazine_tracking_frame::controller;
		p.interaction=physical_reload::with_box_magazine_well(p.interaction);
		p.interaction.magazine_pose_count=static_cast<std::uint8_t>(magazine_grasps.size());
		p.magazine_fills=magazine_fills;return p;
	}(),"weap_fnfal_chamber_plr",false,false);
	static_assert(action_grips.size()==reload_interaction.slide_pose_count);
}
