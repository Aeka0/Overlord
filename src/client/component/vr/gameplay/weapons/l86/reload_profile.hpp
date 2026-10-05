#pragma once
#include "magazine_fill.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "reload_poses.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::l86
{
	inline bool native_family(std::string_view name) noexcept
	{ return native_weapon_family(name,"sa80") || name=="sa80lmg_scope"; }
	inline const reload_profile physical=with_split_sounds([] {reload_profile p{
		.id="l86",
		.native_name="sa80",
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_reload",
		.bullets_bone="j_bullets",
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
		.rigid_magazine_source="h2_viewmodel_sa80_lmg_base"
		};
		p.authored_action_fire=true;p.magazine_fills=magazine_fills;return p;}(),"weap_sa80_chamber_plr",false,false);
	static_assert(action_grips.size()==reload_interaction.slide_pose_count);
}
