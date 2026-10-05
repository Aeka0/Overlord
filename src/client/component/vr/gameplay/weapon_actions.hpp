#pragma once
#include <string_view>

namespace vr::gameplay::weapons
{
	// Only used at the local first-person presentation boundary. Do not apply
	// this classification to NPC/script animation trees or to weapon state IDs.
	inline bool native_equip_clip(std::string_view animation) noexcept
	{
		if (!animation.starts_with("h2_wpn_") && !animation.starts_with("h1_wpn_") &&
			!animation.starts_with("viewmodel_")) return false;
		constexpr std::string_view actions[]{"_pullout","_pullout_first","_first_time_pullout","_first_pullout",
			"_pullout_quick","_pullout_empty","_putaway","_putaway_quick","_putaway_empty",
			"_pullout1","_pullout2","_pullout_first_alt","_empty_putaway","_quick_pullout","_quick_putaway"};
		for (auto suffix:actions) if (animation.ends_with(suffix)) return true;
		return false;
	}
	inline bool native_idle_clip(std::string_view animation) noexcept
	{
		return (animation.starts_with("h2_wpn_") || animation.starts_with("h1_wpn_") ||
			animation.starts_with("viewmodel_")) &&
			(animation.ends_with("_idle") || animation.ends_with("_idle_empty") || animation.ends_with("_empty_idle"));
	}
	inline int equip_presentation_index(int original,int idle,std::string_view original_name,
		std::string_view idle_name,bool local_vr,int side,bool alternate) noexcept
	{
		return local_vr && side==0 && !alternate && original>0 && original<202 && idle>0 && idle<202 &&
			native_equip_clip(original_name) && native_idle_clip(idle_name) ? idle : original;
	}
	inline bool base_equip_action(std::string_view animation, std::string_view prefix) noexcept
	{
		if (!animation.starts_with(prefix)) return false;
		const auto action = animation.substr(prefix.size());
		return action == "pullout" || action == "pullout_first" || action == "first_time_pullout" || action == "first_pullout" ||
			action == "pullout_quick" || action == "pullout_empty" || action == "putaway" ||
			action == "putaway_quick" || action == "putaway_empty" || action == "empty_putaway" ||
			action == "quick_pullout" || action == "quick_putaway";
	}
}
