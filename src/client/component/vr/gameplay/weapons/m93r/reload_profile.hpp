#pragma once
#include "magazine_fill.hpp"
#include "../../physical_reload_profile.hpp"
#include "mechanics.hpp"
#include "poses.hpp"
#include "reload_poses.hpp"
#include "magazine_contacts.hpp"
#include "reload_interaction.hpp"
#include "feedback.hpp"
#include "knife_grips.hpp"
#include "../m9/feedback.hpp"
#include "../m9/mechanics.hpp"

namespace vr::gameplay::weapons::m93r
{
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "m93r",
		                     .native_name = native_name,
		                     .ammunition = reload_rules,
		                     .interaction = reload_interaction,
		                     .magazine_bone = "tag_clip",
		                     .slide_bone = "j_bolt",
		                     .bullets_bone = "j_bullets",
		                     .magazine_rest = magazine_rest,
		                     .slide_rest = action_rest,
		                     .rigid_in_magazine = {},
		                     .magazine_in_wrist = magazine_in_wrist,
		                     .magazine_top = magazine_top,
		                     .well = magazine_well,
		                     .slide_grab_low = action_grab_low,
		                     .slide_grab_high = action_grab_high,
		                     .magazine_fingers = magazine_fingers,
		                     .slide_grips = action_grips,
		                     .sound_key = sound_key,
		                     .magazine_contacts = &contacts,
		                     .rigid_magazine_source = "h2_viewmodel_beretta_393_base"};
		    p.knife_magazine_in_wrist = &knife_magazine_in_wrist;
		    p.knife_slide_grips = knife_slide_grips;
		    // M93R has no isolated rear key; explicitly share M9's existing pull.
		    p.rear_sound = {m9::sound_key(mechanics::effect::action_rear),
		                    sound_reference_kind::notetrack,
		                    sound_part::whole,
		                    m9::native_name};
		    p.interaction.knife_slide_pose_count = static_cast<std::uint8_t>(knife_slide_grips.size());
		    p.magazine_fills = magazine_fills;
		    return p;
	    }(),
	    {
	        .cycle_notetrack = nullptr,
	        .retain_close = false,
	        .split_removal = true,
	    });
	static_assert(action_grips.size() == reload_interaction.slide_pose_count);
}
