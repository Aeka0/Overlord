#pragma once
#include "../../magazine_grasp_profile.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "reload_poses.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::famas
{
	inline bool native_family(std::string_view name) noexcept
	{ return native_weapon_family(name,"famas"); }
	inline const reload_profile physical=with_split_sounds(with_controller_magazine(reload_profile{
		.id="famas",
		.native_name="famas",
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_bolt",
		.bullets_bone="j_bullet",
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
		.rigid_magazine_source="h2_viewmodel_famas_base_arctic"
		}),"weap_famas_chamber_plr",false,true);
	inline const reload_profile tape=[] {
		auto p=physical;p.id="famas_tape";p.rigid_magazine_source="h2_viewmodel_famas_base_tape";return p;
	}();
	inline const reload_profile woodland=[] {
		auto p=physical;p.id="famas_woodland";p.rigid_magazine_source="h2_viewmodel_famas_base_woodland";return p;
	}();
	inline const std::array<const reload_profile*,3> skins{&physical,&tape,&woodland};
	static_assert(action_grips.size()==reload_interaction.slide_pose_count);
}
