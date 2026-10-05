#pragma once
#include "magazine_fill.hpp"
#include "mechanics.hpp"
#include "reload_interaction.hpp"
#include "reload_poses.hpp"
#include "feedback.hpp"

namespace vr::gameplay::weapons::ak47
{
	inline bool native_family(std::string_view name) noexcept
	{
		return native_weapon_family(name, "ak47");
	}
	// Gun +Y is left. Bound acquisition outside the right receiver wall for
	// either hand so angular assistance cannot reach through to the magazine.
	inline constexpr part_capture_halfspace handle_capture{{0, 1, 0}, bolt_grab_high[1]};
	inline const reload_profile physical = with_split_sounds(
	    []
	    {
		    reload_profile p{.id = "ak47",
		                     .native_name = "ak47",
		                     .ammunition = reload_rules,
		                     .interaction = reload_interaction,
		                     .magazine_bone = "tag_clip",
		                     .slide_bone = "j_bolt2",
		                     .bullets_bone = "j_bullet01",
		                     .magazine_rest = magazine_rest,
		                     .slide_rest = bolt_rest,
		                     .rigid_in_magazine = {},
		                     .magazine_in_wrist = magazine_in_wrist,
		                     .magazine_top = magazine_top,
		                     .well = magazine_well,
		                     .slide_grab_low = bolt_grab_low,
		                     .slide_grab_high = bolt_grab_high,
		                     .magazine_fingers = magazine_fingers,
		                     .slide_grips = bolt_grips,
		                     .sound_key = sound_key,
		                     .native_family = native_family,
		                     .magazine_contacts = &contacts,
		                     .additional_bullet_bones = additional_bullets,
		                     .rigid_magazine_source = "h2_viewmodel_ak47_base"};
		    p.slide_capture = &handle_capture;
		    p.magazine_fills = magazine_fills;
		    p.magazine_tracking = magazine_tracking_frame::controller;
		    return p;
	    }(),
	    {
	        .cycle_notetrack = "weap_ak47_chamber_plr",
	        .retain_close = false,
	        .split_removal = false,
	    });
	// Immutable render identities share all mechanics/poses; exact live receiver
	// selection determines camouflage, never a guessed native-name suffix.
	inline const reload_profile arctic = []
	{
		auto p = physical;
		p.id = "ak47_arctic";
		p.rigid_magazine_source = "h2_viewmodel_ak47_base_arctic";
		return p;
	}();
	inline const reload_profile digital = []
	{
		auto p = physical;
		p.id = "ak47_digital";
		p.rigid_magazine_source = "h2_viewmodel_ak47_base_digital";
		return p;
	}();
	// Live ak47_desert_grenadier: identical 25-bone bind/parent layout; retain
	// its actual receiver as the magazine render source, including camouflage.
	inline const reload_profile desert = []
	{
		auto p = physical;
		p.id = "ak47_desert";
		p.rigid_magazine_source = "h2_viewmodel_ak47_base_desert";
		return p;
	}();
	inline const reload_profile woodland = []
	{
		auto p = physical;
		p.id = "ak47_woodland";
		p.rigid_magazine_source = "h2_viewmodel_ak47_base_woodland";
		return p;
	}();
	inline const std::array<const reload_profile*, 5> skins{&physical, &arctic, &digital, &desert, &woodland};
	static_assert(bolt_grips.size() == reload_interaction.slide_pose_count);
}
