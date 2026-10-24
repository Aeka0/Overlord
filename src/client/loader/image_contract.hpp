#pragma once
#include <cstdint>

namespace game_image
{
	inline constexpr std::uintptr_t base_address = 0x140000000;

	// Native hooks use absolute addresses throughout the supported H2 image.
	// Relocating only TLS would leave the rest of that contract invalid.
	[[nodiscard]] inline bool supported_mapping(const std::uintptr_t actual, const std::uintptr_t preferred) noexcept
	{
		return actual == base_address && preferred == base_address;
	}
}
