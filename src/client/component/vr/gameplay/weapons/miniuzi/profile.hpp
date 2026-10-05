#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"
#include "stock_support.hpp"

namespace vr::gameplay::weapons::miniuzi
{
	inline constexpr profile base=[] {
		profile out{"miniuzi","h2_viewmodel_miniuzi_base","folded_stock",aim_rule::two_hand,wrists,1,
			.10f,.22f,.10f,stock_pose,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
		out.wrists[0]=stock_support;
		out.support_mirror_center=&stock_contact_center;
		out.free_hand_reference=&wrists; // Changing support must not rotate reload input.
		return out;
	}();
}
