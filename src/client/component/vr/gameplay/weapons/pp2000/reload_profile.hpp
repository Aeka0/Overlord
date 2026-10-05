#pragma once
#include "magazine_fill.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "reload_poses.hpp"
#include "magazine_contacts.hpp"
#include "folding_handle.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::pp2000
{
	inline bool native_family(std::string_view name) noexcept
	{ return native_weapon_family(name,"pp2000"); }
	// Animation/native weapon stem is pp2000; the H2 model stem is p2000.
	inline const reload_profile physical=[] {reload_profile p{
		.id="pp2000",
		.native_name="pp2000",
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
		.slide_grips=folding_grips,
		.sound_key=sound_key,
		.native_family=native_family,
		.magazine_contacts=&contacts,
		.rigid_magazine_source="h2_viewmodel_p2000_base"
		};
		p.handle_fold=&handle_fold;p.magazine_fills=magazine_fills;return p;}();
	static_assert(folding_grips.size()==reload_interaction.slide_pose_count);
}
