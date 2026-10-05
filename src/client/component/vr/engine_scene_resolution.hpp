#pragma once

#include "engine_scene_extent.hpp"
#include <string>

namespace vr::engine_scene_resolution
{
	enum class phase : std::uint8_t { awaiting_runtime, queued, rebuilding, ready, failed };
	struct report
	{
		phase state{phase::awaiting_runtime};
		bool hooks_installed{};
		extent requested{};
		video_config config{};
		extent color{}, depth{}, desktop{};
		std::uint32_t color_samples{}, depth_samples{};
		std::uint32_t supersample_count{};
		std::uint32_t apply_thread{};
		std::uint64_t rebuilds{};
		std::string error;
	};

	// Loader-only installation. Runtime calls publish a size; only H2's natural
	// main-loop video-update boundary may synchronize/rebuild its resources.
	void install();
	bool request(extent desired, std::string& error);
	bool ready() noexcept;
	bool accepts_source(extent actual) noexcept;
	report get_report();
	const char* to_string(phase value) noexcept;
}
