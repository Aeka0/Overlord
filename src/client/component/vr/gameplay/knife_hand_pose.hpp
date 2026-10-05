#pragma once

namespace vr::gameplay::equipment
{
	// Exact-record attachment style. Grip direction remains independently owned
	// by the chest knife; manipulating a firearm never selects forward/reverse.
	enum class knife_hand_pose { grip, magazine, slide };
}
