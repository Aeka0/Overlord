#pragma once
#include "engine_scene_extent.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vr::render_tuning
{
	inline std::atomic<float> render_scale{1.0f};
	inline std::atomic<float> sharpening{0.0f};
	inline std::atomic<bool> hidden_area_mesh{true};

	inline engine_scene_resolution::extent scale_extent(
	    const std::uint32_t base_width,
	    const std::uint32_t base_height,
	    const std::uint32_t max_width = 16384u,
	    const std::uint32_t max_height = 16384u) noexcept
	{
		const float raw = render_scale.load(std::memory_order_acquire);
		const float s = std::isfinite(raw) ? std::clamp(raw, 0.50f, 1.20f) : 1.0f;
		if (std::abs(s - 1.0f) < 0.005f)
			return {base_width, base_height};
		const auto round8 = [](const float v) noexcept -> std::uint32_t
		{
			const auto rounded = static_cast<std::uint32_t>(std::lround((std::max)(64.0f, v)));
			return (std::max)(64u, (rounded + 4u) & ~7u);
		};
		const std::uint32_t cap_w = (max_width >= 64u) ? (max_width & ~7u) : 16384u;
		const std::uint32_t cap_h = (max_height >= 64u) ? (max_height & ~7u) : 16384u;
		const std::uint32_t w = std::clamp(round8(static_cast<float>(base_width) * s), 64u, cap_w);
		const std::uint32_t h = std::clamp(round8(static_cast<float>(base_height) * s), 64u, cap_h);
		const engine_scene_resolution::extent scaled{w, h};
		return engine_scene_resolution::valid(scaled) ? scaled
		                                              : engine_scene_resolution::extent{base_width, base_height};
	}
}
