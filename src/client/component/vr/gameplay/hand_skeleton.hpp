#pragma once
#include "hand_pose_solver.hpp"
#include <string_view>

namespace vr::gameplay::hands
{
	// Copied engine-independent descriptors. Parents include attachment and
	// duplicate-bone aliases; bind transforms are never the animated DObj pose.
	struct bone_definition
	{
		std::string_view name;
		int parent{-1};
		bone bind{};
	};
	struct model_definition
	{
		std::string_view name;
		int begin{},count{};
	};
}
