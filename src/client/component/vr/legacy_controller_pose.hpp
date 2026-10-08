#pragma once

#include "touch_controller_reference.hpp"
#include <string_view>

namespace vr::legacy_controller_pose
{
	// Temporary rollback boundary. Keep the previous provider-specific reference
	// selection here so retiring either pipeline does not touch gameplay owners.
	inline controller_pose_reference::configuration select(
	    controller_pose_reference::configuration reference, std::string_view runtime)
	{
		if (!reference.expected_runtime.empty() && reference.expected_runtime != runtime)
			reference = {};
		if (runtime == "VirtualDesktopXR")
			reference = controller_pose_reference::touch_legacy_reference();
		return reference;
	}
}
