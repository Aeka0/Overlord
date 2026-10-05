#pragma once
#include <cstdint>

namespace vr::native_fullscreen_blur
{
	struct script_range
	{
		std::uintptr_t begin{},end{};
		bool contains(std::uintptr_t position) const noexcept
		{return begin && end>begin && end-begin<=8192 && position>=begin && position<end;}
	};
	inline bool suppress_damage_request(bool vr,bool local_player,unsigned arguments,
		std::uintptr_t position,script_range damage) noexcept
	{return vr && local_player && arguments==2 && damage.contains(position);}

	// Filter CL_GetMenuBlurRadius's contribution, not the combined refdef.
	// Death, script and HUD contributions still enter the original native sum.
	inline float filter_menu_radius(bool vr,float radius) noexcept {return vr ? 0.f : radius;}
}
