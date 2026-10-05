#pragma once
#include "hand_pose_library.hpp"
#include "bar_hand_pose.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::hands
{
	struct interaction_rig
	{
		hands::pose_library library{};
		std::array<hands::pose_library, 2> vehicle_poses{};
		hands::pose_library vehicle_handles{};
		std::array<hands::quat, 2> basis{};
		std::array<bar_grip::binding, 2> bar_grips{};
		bool valid{};
	};
	interaction_rig bind_interaction_rig(const rig&, std::span<const bone_definition>) noexcept;
	struct presentation_input
	{
		const rig& skeleton;
		const controller_input::frame& controllers;
		const std::array<anchor, 2>& targets;
		const std::array<vec, 2>& shoulders;
		const std::array<vec, 3>& axes;
		float units_per_meter;
		std::span<bone> solved;
		unsigned posed_hands{};
		unsigned visible_hands{3};
	};
	struct presentation_result
	{
		std::uint64_t knife_revision{};
		std::array<std::uint64_t, 2> reload_item_tokens{};
	};
	presentation_result present_interactions(const interaction_rig&, const presentation_input&) noexcept;
}
