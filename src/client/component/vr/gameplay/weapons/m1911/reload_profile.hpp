#pragma once
#include "magazine_fill.hpp"
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "reload_interaction.hpp"
#include "feedback.hpp"
#include "knife_slide_grip.hpp"

namespace vr::gameplay::weapons::m1911
{
	inline const reload_profile physical = with_magazine_population(
	    []
	    {
		    reload_profile value{.id = "m1911",
		                         .native_name = native_name,
		                         .ammunition = reload_rules,
		                         .interaction = reload_interaction,
		                         .magazine_bone = "tag_clip",
		                         .slide_bone = "j_bolt",
		                         .bullets_bone = "tag_bullets",
		                         .magazine_model = "h2_weapon_colt45_clip",
		                         .magazine_rest = equip_rest[2].local,
		                         .slide_rest = equip_rest[0].local,
		                         .rigid_in_magazine = rigid_in_magazine,
		                         .magazine_in_wrist = magazine_in_wrist,
		                         .magazine_top = magazine_top,
		                         .well = magazine_well,
		                         .slide_grab_low = slide_grab_low,
		                         .slide_grab_high = slide_grab_high,
		                         .magazine_fingers = magazine_fingers,
		                         .slide_grips = slide_grips,
		                         .sound_key = sound_key};
		    value.knife_magazine_in_wrist = &knife_magazine_in_wrist;
		    value.interaction.support_magazine_catch = true;
		    value.knife_slide_grips = knife_slide_grips;
		    value.interaction.knife_slide_pose_count = static_cast<std::uint8_t>(knife_slide_grips.size());
		    return value;
	    }(),
	    magazine_fills,
	    "h2_viewmodel_colt45_base");
	static_assert(slide_grips.size() == reload_interaction.slide_pose_count);
}
