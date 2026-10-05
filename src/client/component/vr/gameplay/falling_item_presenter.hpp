#pragma once
#include "falling_trajectory.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "ejection_scatter.hpp"
#include "weapon_identity.hpp"

namespace game {struct XModel;}
namespace vr::gameplay::weapons {struct reload_profile;}
namespace vr::gameplay::falling_item_presentation
{
	// Immutable cosmetic motion; inventory, catches and collision stay with
	// their simulation owners. All pieces share the same absolute trajectory.
	struct motion
	{
		::vr::gameplay::motion::flight path{};
		hands::anchor local{};
		weapons::ejection_scatter::motion scatter{};
		hands::quat scatter_basis{0,0,0,1};
		bool scattered{};
		weapons::weapon_identity origin{};
		const weapons::reload_profile* rail_profile{};
		hands::anchor pose(::vr::gameplay::motion::clock::time_point at,const hands::anchor* rail_root=nullptr)const noexcept
		{
			auto root=rail_root?*rail_root:path.pose(at);
			if(scattered)root=weapons::ejection_scatter::apply(root,scatter_basis,scatter,
				std::chrono::duration<float>(at-path.born).count(),path.units);
			return hands::pose_math::compose(root,local);
		}
	};
	// Coarse scene admission followed by exact motion at native surface packing.
	// Uses retained submission tickets, never the newest occupant of a drop slot.
	void submit(game::XModel*,const motion&,std::uint64_t reference,float cull_padding);
}
