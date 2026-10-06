#pragma once
#include "controller_pose_reference.hpp"

namespace vr::steamvr
{
	// SteamVR/OpenXR only. A short Utility connection copies static controller
	// component metadata and closes before loading OpenXR. No poses,
	// compositor calls, bindings, GPU resources or SDK handles are retained.
	controller_pose_reference::configuration query_openxr_grip_reference();
}
