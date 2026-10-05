#pragma once
#include "pose_solver.hpp"
#include <string_view>

namespace vr::gameplay::hands
{
	struct joint_pose {std::string_view name;quat rotation;};
	struct part_pose {std::string_view name;anchor local;};
	struct pose_schema
	{
		std::string_view id{};
		std::span<const joint_pose> fingers{};
		std::span<const part_pose> equip_rest{};
		std::array<anchor,2> wrists{};
		const std::array<anchor,2>* free_hand_reference{};
	};
	template<class Pose> inline quat free_hand_rotation(const Pose& pose,int hand) noexcept
	{return (pose.free_hand_reference?*pose.free_hand_reference:pose.wrists)[hand].rotation;}
}

namespace vr::gameplay
{
	using hands::joint_pose;
	using hands::part_pose;
}
