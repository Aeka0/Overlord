#pragma once
#include "notebook_policy.hpp"
#include "component/scene_skeletal_model.hpp"
#include <atomic>

namespace vr::gameplay::equipment::special::notebook
{
	struct model
	{
		game::XModel* source{};
		scene_models::skeletal_model skeleton;
		std::array<anchor,11> bind{};
		std::array<unsigned,11> group{};
		std::atomic_uint64_t submissions{},rejected{};
		bool create(game::XModel*);
		bool submit(anchor root,float angle);
	};
}
