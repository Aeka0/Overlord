#pragma once
#include "weapon_profile.hpp"

namespace vr::gameplay::weapons
{
	struct animation_presentation
	{
		bool valid{}, equip{};
	};
	bool initialize_animation_query();
	// Caller holds the native first-person DObj lock. Read-only; never patches
	// shared XAnim assets, weapon timers, notetracks or NPC trees.
	animation_presentation sample_animation_presentation(const void* tree, const profile& profile) noexcept;
} // namespace vr::gameplay::weapons
