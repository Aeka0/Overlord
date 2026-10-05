#pragma once
#include "weapon_profile.hpp"

namespace vr::gameplay::weapons
{
	enum class magazine_grasp_kind { native, bottom_pinch, body_wrap };
	enum class magazine_grasp_policy { wrist_facing, body_palm, fixed };
	enum class magazine_tracking_frame { weapon_wrist, controller };
	// A complete left-hand recipe. The rigid magazine is never scaled/mirrored;
	// hands::pose_mirror carries the attachment, contact and anatomical fingers together.
	struct magazine_grasp_pose
	{
		hands::anchor in_wrist;
		hands::vec contact_in_wrist;
		std::span<const joint_pose> fingers;
		magazine_grasp_kind kind{magazine_grasp_kind::native};
	};
}
