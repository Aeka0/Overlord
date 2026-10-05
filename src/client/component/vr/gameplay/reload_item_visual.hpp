#pragma once
#include "hand_pose_library.hpp"
#include <array>
namespace game {struct XModel;}
namespace vr::gameplay::reload_items
{
	struct visual_piece {game::XModel* model{};hands::anchor in_item{};};
	struct visual
	{
		std::array<visual_piece,13> pieces{};size_t count{};
		explicit operator bool()const noexcept{return count && pieces[0].model;}
	};
}
