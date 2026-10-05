#pragma once
#include "body_equipment.hpp"
#include <cstring>
#include <span>

namespace vr::gameplay::equipment
{
	inline constexpr std::array item_slots{slot::tactical,slot::lethal};
	// H2 selected secondary/primary offhand, not the currently primed offhand at
	// 0x3b0. Verified in native selection code and live frag/flash inventory.
	inline std::array<std::uint32_t,2> selected_chest_items(std::span<const std::byte> ps) noexcept
	{
		std::array<std::uint32_t,2> out{};
		if(ps.size()<0x3bc)return out;
		for(unsigned i=0;i<out.size();++i)
		{
			std::memcpy(&out[i],ps.data()+(i==0 ? 0x3b8:0x3b4),4);
			if(out[i]>511)out[i]=0;
		}
		return out;
	}
	inline bool show_chest_item(int loaded,int reserve) noexcept
	{return loaded>=0 && loaded<=1001 && reserve>=0 && reserve<=1000000 && (loaded>0 || reserve>0);}
	inline anchor stowed_chest_item(const chest_slots& chest,slot where,vec model_center) noexcept
	{
		const auto& point=chest.anchors[unsigned(where)];
		// World equipment is upright along model +Z. Knife slot basis has blade
		// +X downward: rotate the item back into body forward/left/up axes.
		const auto rotation=normalize(multiply(point.rotation,quat{0,-.70710678f,0,.70710678f}));
		return {sub(point.position,rotate(rotation,model_center)),rotation};
	}
}
