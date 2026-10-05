#pragma once
#include <array>
#include <cstdint>
#include <utils/hook_validation.hpp>

namespace vr::native_thermal
{
	inline bool ready() noexcept
	{
		// Shared by mounted and carried optics. Bit 0 selects native heat/color;
		// bit 1 removes the flat scope stencil and ordinary-color redraw.
		static const bool verified=[]
		{
			constexpr std::uint8_t bytes[]{0x0f,0xb6,0x8f,4,2,0,0,0x0f,0xb6,0xc1,0x24,1,0x74,0x0a,0xf6,0xc1,2};
			std::array<std::uint8_t,sizeof(bytes)> mask;mask.fill(255);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1407B016F),{bytes,mask.data(),mask.size()}));
		}();
		return verified;
	}
	inline bool world_ready() noexcept
	{
		static const bool verified=[]
		{
			// Proven native SSR producer (public scale * thermal fade) and the
			// backend consumer that rebuilds SSR shader constants for each view.
			constexpr std::uint8_t writer[]{0x48,0x8b,5,0x29,0xc0,0x6b,0x0e,0xf3,0x0f,0x10,0x40,0x10,
				0x48,0x8b,5,0x15,0xc0,0x6b,0x0e,0xf3,0x0f,0x59,0x40,0x10,0xf3,0x0f,0x11,0x83,0xa4,0x2c,0,0};
			constexpr std::uint8_t consumer[]{0xf3,0x44,0x0f,0x10,0x91,0xa4,0x2c,0,0};
			const auto check=[](std::uintptr_t at,const auto& bytes)
			{
				std::array<std::uint8_t,sizeof(bytes)> mask;mask.fill(255);
				return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),{bytes,mask.data(),mask.size()}));
			};
			return ready() && check(0x14077EBC0,writer) && check(0x1407815A0,consumer);
		}();
		return verified;
	}
}
