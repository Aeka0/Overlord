#pragma once
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "magazine_contacts.hpp"
#include "reload_interaction.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::g18
{
	inline const reload_profile physical=with_split_sounds(reload_profile{
		.id="g18",
		.native_name=native_name,
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_bolt",
		.bullets_bone="j_bullet",
		.magazine_rest=equip_rest[2].local,
		.slide_rest=equip_rest[0].local,
		.rigid_in_magazine={},
		.magazine_in_wrist=magazine_in_wrist,
		.magazine_top=magazine_top,
		.well=magazine_well,
		.slide_grab_low=slide_grab_low,
		.slide_grab_high=slide_grab_high,
		.magazine_fingers=magazine_fingers,
		.slide_grips=slide_grips,
		.sound_key=sound_key,
		.magazine_contacts=&contacts,
		.rigid_magazine_source="h2_viewmodel_glock_base",
		.receiver_parented_bullets=true
		},"weap_glock_first_lift_chamber_plr",true,false);
	static_assert(slide_grips.size()==reload_interaction.slide_pose_count);
}
