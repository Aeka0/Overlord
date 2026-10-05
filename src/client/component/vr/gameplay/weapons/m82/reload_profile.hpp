#pragma once
#include "../../magazine_grasp_profile.hpp"
#include "reload_poses.hpp"
#include "magazine_grasps.hpp"

namespace vr::gameplay::weapons::m82
{
	// The playable native weapon is "barrett"; m82 is its exported model/animation family.
	inline bool native_family(std::string_view name) noexcept
	{
		return native_weapon_family(name, "barrett");
	}
	inline constexpr mechanics::rules reload_rules{10, mechanics::magazine_release::physical_pull, false, false, true};
	// Manual straight-down extraction; spare-magazine striking is not enabled.
	inline constexpr physical_reload::magazine_manipulation manual_magazine{.05f, .05f, .10f,  {0, 0, -1}, .025f,
																			.06f, .15f, .012f, {1, 0, 0},  false};
	inline constexpr physical_reload::profile reload_interaction{.18f,
																 part_grip_capture::radius_m,
																 action_stroke_m,
																 0.f,
																 action_stroke_m * .95f,
																 .18f,
																 .045f,
																 .06f,
																 .08715574f,
																 .25f,
																 {-1, 0, 0},
																 .06f,
																 .35f,
																 .04f,
																 .003f,
																 2,
																 physical_reload::action_motion::reciprocating_slide,
																 &manual_magazine};
	inline const char *sound_key(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out:
		case magazine_take:
			return "weap_m82_clipout_plr";
		case magazine_in:
			return "weap_m82_clipin_plr";
		case action_close:
			return "weap_m82_chamber_close_plr";
		default:
			return nullptr;
		}
	}
	inline constexpr std::array<std::string_view, 1> extra_rounds{"tag_bullet_single"};
	inline constexpr std::array<part_parent_contract, 3> round_parents{
		{{"tag_bullet2", "tag_clip"}, {"tag_bullet", "tag_bullet2"}, {"tag_bullet_single", "tag_bullet"}}};
	inline const reload_profile physical=with_split_sounds([] {
		reload_profile p{
		.id="m82",
		.native_name="barrett",
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_bolt",
		.bullets_bone="tag_bullet",
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
		.rigid_magazine_source="h2_viewmodel_m82_base"
		};
		p.additional_bullet_bones = extra_rounds;
		p.bullet_parents = round_parents;
		p.magazine_grasps=magazine_grasps;
		return with_controller_magazine(p);
	}(),"weap_m82_chamber_close_plr",false,true);
	inline const std::array<const reload_profile *, 1> skins{&physical};
} // namespace vr::gameplay::weapons::m82
