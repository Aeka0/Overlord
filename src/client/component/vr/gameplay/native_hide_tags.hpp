#pragma once
#include "viewmodel_policy.hpp"
#include <span>

namespace vr::gameplay::weapons
{
	// Bind WeaponDef.hideTags to THIS assembled object's bone order. Native
	// tokens are sufficient; no localized strings or weapon-name heuristics.
	inline bool bind_native_hide_tags(std::span<const std::uint32_t> bones,
		std::span<const std::uint32_t> tags, part_mask& out) noexcept
	{
		if (bones.empty() || bones.size()>254 || tags.size()>32) return false;
		part_mask result{};
		for (std::size_t i=0;i<bones.size();++i)
		{
			if (!bones[i]) return false;
			for (auto tag : tags) if (tag && tag==bones[i])
			{ result[i/32] |= 0x80000000u>>(i%32); break; }
		}
		// Missing tags are normal across shared definitions/optional models.
		out=result;return true;
	}
}
