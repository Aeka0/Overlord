#pragma once
#include "magazine_fill.hpp"
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "reload_interaction.hpp"
#include "feedback.hpp"
#include "knife_slide_grip.hpp"

namespace vr::gameplay::weapons::de50
{
	// The third cartridge is a child of the existing two-round group. Keep
	// this hierarchy explicit when selecting rigid geometry and hiding rounds.
	inline constexpr std::array<std::string_view, 1> additional_round_bones{"j_bullet01"};
	inline constexpr std::array<part_parent_contract, 1> round_parents{{{"j_bullet01", "tag_bullets"}}};
	inline bool native_family(std::string_view name) noexcept
	{
		return name == native_name || name == "deserteagle_gold";
	}
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile value{.id = "de50",
		                         .native_name = native_name,
		                         .ammunition = reload_rules,
		                         .interaction = reload_interaction,
		                         .magazine_bone = "tag_clip",
		                         .slide_bone = "j_bolt",
		                         .bullets_bone = "tag_bullets",
		                         .magazine_rest = equip_rest[2].local,
		                         .slide_rest = equip_rest[0].local,
		                         .magazine_in_wrist = magazine_in_wrist,
		                         .magazine_top = magazine_top,
		                         .well = magazine_well,
		                         .slide_grab_low = slide_grab_low,
		                         .slide_grab_high = slide_grab_high,
		                         .magazine_fingers = magazine_fingers,
		                         .slide_grips = slide_grips,
		                         .sound_key = sound_key,
		                         .native_family = native_family};
		    value.knife_magazine_in_wrist = &knife_magazine_in_wrist;
		    value.rigid_magazine_source = "h2_viewmodel_desert_eagle_base";
		    value.magazine_fills = magazine_fills;
		    value.additional_bullet_bones = additional_round_bones;
		    value.bullet_parents = round_parents;
		    value.interaction.support_magazine_catch = true;
		    value.knife_slide_grips = knife_slide_grips;
		    value.interaction.knife_slide_pose_count = static_cast<std::uint8_t>(knife_slide_grips.size());
		    // Stock reload_empty emits this whole cue while the slide is already
		    // locked back. Preserve the complete release sound on close.
		    return value;
	    }(),
	    {
	        .cycle_notetrack = "weap_de50_chamber_plr",
	        .retain_close = true,
	    });
	static_assert(slide_grips.size() == reload_interaction.slide_pose_count);

	// A distinct immutable asset recipe is needed for the gold receiver's
	// surface/material layout. Its family id, mechanics and grasp poses are shared.
	inline const reload_profile gold_physical = []
	{
		auto value = physical;
		value.native_name = "deserteagle_gold";
		value.rigid_magazine_source = "h2_viewmodel_desert_eagle_gold";
		return value;
	}();
}
