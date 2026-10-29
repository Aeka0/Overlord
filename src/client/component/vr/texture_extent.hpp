#pragma once
#include <cstdint>

namespace vr::texture_extent
{
	// D3D11 FL11 Texture2D limit. Allocation/format support remains the device's
	// decision; pixel-count heuristics must not reject otherwise valid displays.
	inline constexpr std::uint32_t maximum_dimension = 16384;
	constexpr bool valid(std::uint32_t width, std::uint32_t height) noexcept
	{return width && height && width <= maximum_dimension && height <= maximum_dimension;}
}
