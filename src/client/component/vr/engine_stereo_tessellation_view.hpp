#pragma once

#include "engine_stereo_view.hpp"
#include <cmath>
#include <cstring>

namespace vr::engine_stereo_tessellation
{
	// R_SetTessellationViewConstants (29E9D0): 35 views, including shadows.
	// GPU tessellation is shared by both eyes and must use the frontend union.
	inline constexpr std::size_t constants_size = 0x1600;
	struct shared_view
	{
		std::array<std::uint8_t, engine_stereo_view::h2_view_slot_size> culling{}, rendered{};
	};

	[[nodiscard]] inline bool replace_main_view(void* constants, const std::size_t size,
		const shared_view& view) noexcept
	{
		if (!constants || size < constants_size) return false;
		struct field { std::size_t destination, source, size; };
		constexpr field fields[]{{0x20, 0x80, 64}, {0x8e0, 0x40, 64}, {0x11a0, 0x100, 12}};
		auto* const output = static_cast<std::uint8_t*>(constants);
		// Validate the native upload before writing anything. Do not replace
		// another view's data, shadow entries, tessellation factors or origin.w.
		for (const auto& field : fields)
		{
			if (std::memcmp(output + field.destination, view.rendered.data() + field.source, field.size))
				return false;
			for (std::size_t i = 0; i < field.size; i += sizeof(float))
			{
				float value{};
				std::memcpy(&value, view.culling.data() + field.source + i, sizeof(value));
				if (!std::isfinite(value)) return false;
			}
		}
		for (const auto& field : fields)
			std::memcpy(output + field.destination, view.culling.data() + field.source, field.size);
		return true;
	}
}
