#pragma once
#include <string_view>

namespace vr::gameplay::weapons::m9
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		// Exact family prefix; never suppress unrelated scripted prop/NPC clips.
		constexpr std::string_view prefix = "h2_wpn_pst_m9_";
		if (!animation.starts_with(prefix))
			return false;
		const auto action = animation.substr(prefix.size());
		return action == "pullout" || action == "pullout_first" || action == "pullout_quick" ||
			   action == "pullout_empty" || action == "putaway" || action == "putaway_quick" ||
			   action == "putaway_empty";
	}
} // namespace vr::gameplay::weapons::m9
