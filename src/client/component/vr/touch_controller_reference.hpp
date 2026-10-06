#pragma once
#include "controller_pose_reference.hpp"

namespace vr::controller_pose_reference
{
	inline constexpr auto touch_profile = "/interaction_profiles/oculus/touch_controller";

	// Canonical legacy Touch frame for existing wrist settings, not a measured
	// anatomical wrist or VDXR's internal OVR pose. The OpenXR grip is standardized;
	// its legacy relation is identical in SteamVR's CV1/Rift S/Quest/Quest 2/Plus/Pro
	// openxr_grip component definitions. Metres and a right-handed X rotation.
	// See docs/vr-runtime-rendering.md for the reference contract and provenance.
	inline configuration touch_legacy_reference()
	{
		configuration reference;
		reference.target = basis::calibration_frame;
		reference.expected_runtime = "VirtualDesktopXR";
		reference.name = "touch_legacy_reference";
		reference.required_profile = touch_profile;
		constexpr float grip_pitch_degrees = 20.6f;
		constexpr float pitch_radians = grip_pitch_degrees * pose_filter::radians;
		const float c = std::cos(pitch_radians), s = std::sin(pitch_radians);
		for (unsigned hand = 0; hand < reference.hands.size(); ++hand)
		{
			const pose_filter::pose legacy_from_grip{{hand == 0 ? .007f : -.007f, -.00182941f, .1019482f},
			                                         {{{1, 0, 0}, {0, c, -s}, {0, s, c}}}};
			reference.hands[hand] = {.grip_from_calibration = pose_filter::inverse(legacy_from_grip),
			                         .reference_id = hand == 0 ? "touch_legacy_left" : "touch_legacy_right",
			                         .ready = true};
		}
		return reference;
	}
}
