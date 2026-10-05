#pragma once
#include "edge_handle.hpp"

namespace vr::gameplay::weapons::hand_poses::side_pinky_up
{
	// A left-side handle is approached from underneath. Rotate the authored
	// curled-pinky hand around the handle without flipping the wrist through
	// the receiver. The opposite glove uses the same anatomical pinky chain;
	// its canonical wrist faces rearward to keep that hand outside the gun.
	inline part_grip_pose at(hands::vec contact) noexcept
	{
		const auto local=edge_handle::source_styles[1].contact_in_wrist;
		const auto left=edge_handle::source_styles[0].wrist.rotation;
		const auto opposite=hands::normalize(hands::multiply(hands::quat{0,0,1,0},left));
		const auto left_wrist=hands::anchor{hands::sub(contact,hands::rotate(left,local)),left};
		const auto opposite_wrist=hands::anchor{hands::sub(contact,hands::rotate(opposite,local)),opposite};
		return {"pinky_up",left_wrist,local,edge_handle::pinky_fingers,nullptr,
			part_grip_fit{opposite_wrist,local,edge_handle::pinky_fingers}};
	}
}
