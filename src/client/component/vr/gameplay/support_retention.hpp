#pragma once
#include "weapon_profile.hpp"
#include "hands/pose_math.hpp"

namespace vr::gameplay::weapons
{
	enum class support_retention { retained, suspended, released };
	// Test the current solved contact, never the displaced one-hand acquire
	// anchor. Missing tracking/geometry suspends steering without changing grasp ownership.
	inline support_retention retained_support(const profile& p, hands::anchor gun, hands::anchor contact,
		hands::vec rear, hands::vec other, float units, bool tracking_ready) noexcept
	{
		if (!tracking_ready || !std::isfinite(units) || units <= 0 ||
			!std::isfinite(p.release_meters) || p.release_meters <= 0)
			return support_retention::suspended;
		const float separation = hands::length(hands::sub(other, rear)) / units;
		const float distance = hands::length(hands::sub(other, hands::pose_math::compose(gun, contact).position)) / units;
		if (!std::isfinite(separation) || !std::isfinite(distance)) return support_retention::suspended;
		if (!p.support_enabled || (p.aiming == aim_rule::two_hand && separation < .08f) || distance > p.release_meters)
			return support_retention::released;
		return support_retention::retained;
	}
}
