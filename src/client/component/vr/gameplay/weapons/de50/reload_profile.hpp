#pragma once
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "reload_interaction.hpp"
#include "feedback.hpp"
#include "knife_slide_grip.hpp"

namespace vr::gameplay::weapons::de50
{
	inline bool native_family(std::string_view name)noexcept{return name==native_name || name=="deserteagle_gold";}
	inline const reload_profile physical=with_split_sounds([] { reload_profile value{
		.id="de50",
		.native_name=native_name,
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_bolt",
		.bullets_bone="tag_bullets",
		.magazine_model="h2_weapon_desert_eagle_clip",
		.magazine_rest=equip_rest[2].local,
		.slide_rest=equip_rest[0].local,
		.rigid_in_magazine=rigid_in_magazine,
		.magazine_in_wrist=magazine_in_wrist,
		.magazine_top=magazine_top,
		.well=magazine_well,
		.slide_grab_low=slide_grab_low,
		.slide_grab_high=slide_grab_high,
		.magazine_fingers=magazine_fingers,
		.slide_grips=slide_grips,
		.sound_key=sound_key,
		.native_family=native_family
		};
		value.knife_magazine_in_wrist=&knife_magazine_in_wrist;
		value.interaction.support_magazine_catch=true;
		value.knife_slide_grips=knife_slide_grips;value.interaction.knife_slide_pose_count=static_cast<std::uint8_t>(knife_slide_grips.size());
		// Stock reload_empty emits this whole cue while the slide is already
		// locked back. Preserve the complete release sound on close.
		return value;}(),"weap_de50_chamber_plr",true);
	static_assert(slide_grips.size() == reload_interaction.slide_pose_count);
}

