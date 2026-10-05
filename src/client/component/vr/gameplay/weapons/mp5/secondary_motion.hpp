#pragma once
#include "poses.hpp"
#include "../../secondary_motion.hpp"

namespace vr::gameplay::weapons::mp5
{
	// Existing native front strap chain only. Base ring stays rigid. The rest
	// fold and inward angular limits keep the nylon on the receiver's left side.
	inline constexpr auto strap_links=[] {
		std::array<secondary_motion_link,3> out{};
		constexpr std::string_view names[]{"j_front_strap","j_front_strap_mid","j_front_strap_end"};
		for (size_t n=0;n<out.size();++n)
		{
			for (const auto& part:equip_rest) if (part.name==names[n]) out[n].rest=part;
			out[n].lower={n ? -.06f : -.50f,-.25f,-.10f};
			out[n].upper={.25f,.25f,.10f};
		}
		return out;
	}();
	inline constexpr secondary_motion_profile strap{strap_links,{0,0,-.35f},90,11,4};
}
