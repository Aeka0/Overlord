#pragma once

#include <cstddef>
#include <cstdint>

namespace vr::engine_scene_resolution
{
	struct extent
	{
		std::uint32_t width{};
		std::uint32_t height{};
		constexpr bool operator==(const extent&) const noexcept = default;
	};

	// H2 consumes signed 16-bit viewports; D3D11 limits 2D textures to 16384.
	// Never clamp the runtime's recommendation into an accepted lower resolution.
	constexpr bool valid(const extent size) noexcept
	{
		return size.width > 0 && size.height > 0 &&
			size.width <= 16384 && size.height <= 16384;
	}

	// H2 0x14074F670 calculates these fields; 0x1407502D0 stores them in
	// vidConfig. Scene and display are distinct domains, not interchangeable.
	struct window_parameters
	{
		std::uint64_t window;
		std::int32_t x, y;
		std::uint32_t flags, mode;
		float display_aspect;
		std::uint32_t packed_samples;
		extent scene_base;                 // +0x20, before H2 supersampling
		extent scene;                      // +0x28, rasterization/RT/depth
		extent display;                    // +0x30, swapchain/window
		extent aspect_adjusted_display;    // +0x38
		std::uint32_t unknown_40;
		std::uint32_t supersample_count;   // +0x44, 1/2/4/8/16
	};
	static_assert(sizeof(window_parameters) == 0x48);
	static_assert(offsetof(window_parameters, scene_base) == 0x20);
	static_assert(offsetof(window_parameters, scene) == 0x28);
	static_assert(offsetof(window_parameters, display) == 0x30);
	static_assert(offsetof(window_parameters, supersample_count) == 0x44);

	inline bool apply_scene_extent(window_parameters& parameters, const extent desired) noexcept
	{
		// The per-eye resolution is chosen by SteamVR. H2's separate scene SSAA
		// grid is not silently disabled or multiplied into a different contract.
		if (!valid(desired) || !valid(parameters.display) || parameters.supersample_count != 1)
			return false;
		parameters.scene_base = desired;
		parameters.scene = desired;
		return true;
	}

	struct video_config
	{
		extent scene;                      // 0x14EEE0CF0
		extent scene_base;
		extent display;
		std::uint32_t flags;
		extent aspect_adjusted_display;
		std::uint32_t mode;
		std::uint32_t packed_samples;
		float display_aspect;
		float scene_aspect;
		float pixel_aspect;
	};
	static_assert(sizeof(video_config) == 0x38);
	static_assert(offsetof(video_config, display) == 0x10);
	static_assert(offsetof(video_config, scene_aspect) == 0x30);

	constexpr bool matches_scene(const video_config& config, const extent desired) noexcept
	{
		return valid(desired) && config.scene == desired && config.scene_base == desired;
	}
}
