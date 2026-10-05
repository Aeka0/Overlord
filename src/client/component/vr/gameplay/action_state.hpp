#pragma once

namespace vr::gameplay::weapons
{
	// A caught open-bolt sear is ready to fire; a closed-bolt follower lock is
	// not. Keep those states distinct even though both look open at rest.
	enum class action_state { closed, locked_open, held_open, cocked_open, latched_open, manual_unlocked };
}
