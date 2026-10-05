#pragma once
#include "npc_collision_policy.hpp"

namespace vr::gameplay::weapons
{
	// The eye-to-muzzle sweep protects cover. Actors are valid targets even
	// when the physical muzzle has moved into their collision volume.
	inline constexpr int muzzle_clearance_mask=0x280e831 & ~int(npc_collision::actor_contents);
	static_assert(muzzle_clearance_mask==0x2802831);
}
