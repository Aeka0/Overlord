#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"
#include "stock_support.hpp"

namespace vr::gameplay::weapons::miniuzi
{
	inline constexpr profile base = []
	{
		profile out{
		    .id = "miniuzi",
		    .receiver = "h2_viewmodel_miniuzi_base",
		    .variant = "folded_stock",
		    .aiming = aim_rule::two_hand,
		    .wrists = wrists,
		    .authored_rear = 1,
		    .acquire_meters = profile_defaults::acquire_meters,
		    .release_meters = profile_defaults::release_meters,
		    .blend_seconds = profile_defaults::blend_seconds,
		    .fingers = stock_pose,
		    .equip_rest = equip_rest,
		    .suppress_equip = suppress_equip,
		    .reload = &physical,
		    .viewmodel =
		        {
		            .visibility = part_visibility::rigid_groups,
		        },
		};
		out.wrists[0] = stock_support;
		out.support_mirror_center = &stock_contact_center;
		out.free_hand_reference = &wrists; // Changing support must not rotate reload input.
		return out;
	}();
}
