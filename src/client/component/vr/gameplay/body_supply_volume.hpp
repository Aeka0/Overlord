#pragma once
#include "body_reach_volume.hpp"
#include "body_supply_layout.hpp"

namespace vr::gameplay
{
	using supply_volume=body_reach_volume;
	inline std::array<supply_volume,2> body_supply_volumes(hands::vec head,const std::array<hands::vec,3>& axis,
		float units,const body_supply_layout& layout,float radius) noexcept
	{
		std::array<supply_volume,2> out{};
		for (unsigned h=0;h<2;++h)
		{
			auto& v=out[h];
			const auto origin=hands::add(head,hands::add(hands::scale(axis[1],(h ? -1.f : 1.f)*layout.half_width*units),hands::scale(axis[2],-layout.down*units)));
			v=waist_reach(origin,axis,units,h,layout.height,radius);
		}
		return out;
	}
}
