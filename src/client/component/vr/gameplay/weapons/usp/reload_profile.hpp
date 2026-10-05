#pragma once
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "reload_interaction.hpp"
#include "feedback.hpp"
#include "knife_slide_grip.hpp"

namespace vr::gameplay::weapons::usp
{
	inline const reload_profile physical=with_split_sounds([] { reload_profile value{
		.id="usp",
		.native_name=native_name,
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_bolt",
		.bullets_bone="tag_bullets",
		.magazine_model="h2_weapon_usp_clip",
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
		.sound_key=sound_key
		};
		value.knife_magazine_in_wrist=&knife_magazine_in_wrist;
		value.sound_override=sound_override;
		value.interaction.support_magazine_catch=true;
		value.knife_slide_grips=knife_slide_grips;value.interaction.knife_slide_pose_count=static_cast<std::uint8_t>(knife_slide_grips.size());
		return value;}(),"pull_slide",true,true);
	static_assert(slide_grips.size() == reload_interaction.slide_pose_count);
	// Distinct native identity/instance admission, shared authored mechanism,
	// poses and magazine asset. No duplicated geometry or state-machine logic.
	inline const reload_profile silenced_physical = [] {
		auto value=physical;
		value.id="usp_silencer";
		value.native_name=silenced_native_name;
		return value;
	}();
}
