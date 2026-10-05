#pragma once
#include "side_pinky_up.hpp"

namespace vr::gameplay::weapons::hand_poses::left_handle
{
	inline std::array<part_grip_pose,2> with_native(const part_grip_pose& native) noexcept
	{
		const auto contact=hands::add(native.wrist.position,hands::rotate(native.wrist.rotation,native.contact_in_wrist));
		std::array<part_grip_pose,2> out{native,side_pinky_up::at(contact)};
		// Reflect the AK's LEFT hooks anatomically onto the right glove. Their
		// wrist is on the receiver-right side before reflection, so the rendered
		// right hand stays outside a left-side tab. Reusing the native left-hand
		// wrist (or AK's opposite fit) instead flips that glove into the receiver.
		// Contacts stay on the real tab; catch motion carries the resolved hand
		// afterwards, in the same order as ordinary charging-handle presentation.
		const auto hooks=edge_handle::at(contact);
		for(size_t i=0;i<out.size();++i)
			out[i].opposite_pose=part_grip_fit{hooks[i].wrist,hooks[i].contact_in_wrist,hooks[i].fingers};
		return out;
	}
}
