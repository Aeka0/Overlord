#pragma once
#include <cstdint>

namespace vr::gameplay::weapons::native_carry_selection
{
	enum class admission {native,defer,physical};
	inline admission admit(std::uint32_t target,std::uint32_t held,std::uint32_t pending,
		std::uint32_t actual,std::uint32_t flags,int state,bool alternate)noexcept
	{
		if(alternate || (state!=3 && state!=4))return admission::native;
		// The mission's immediate switch clears actual selection before its
		// client command arrives. An older command must not consume that switch
		// by raising the other gun again. Keep the native transition pending.
		constexpr std::uint32_t immediate_locked=0x08000800;
		if(pending && target!=pending && !actual && (flags&immediate_locked)==immediate_locked)
			return admission::defer;
		return target && (target==held || target==pending)?admission::physical:admission::native;
	}
}
