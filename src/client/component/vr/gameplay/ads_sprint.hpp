#pragma once

namespace game { struct playerState_s; }

namespace vr::gameplay::ads_sprint
{
	// Shared by native sprint predicates and the optional movement profile.
	// Both authoritative and predicted local movement must use the same policy.
	bool allowed(const game::playerState_s* ps) noexcept;
}
