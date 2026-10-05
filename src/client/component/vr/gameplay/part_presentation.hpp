#pragma once
#include "viewmodel_policy.hpp"
#include "knife_hand_pose.hpp"
#include "part_hand_transition.hpp"

namespace vr::gameplay::weapons::part_presentation
{
	struct result
	{
		bool valid{};
		part_mask hidden{}; // Mechanical parts only; compose assembly policy once after all pose work.
		unsigned posed_hands{}; // Preserve the final manipulation wrists/fingers.
		std::array<equipment::knife_hand_pose,2> knife_poses{};
	};
}
