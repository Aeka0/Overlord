#pragma once
#include <atomic>

namespace vr::presentation_options
{
	// Published on the main pipeline; renderers never query or mutate native dvars.
	inline std::atomic_bool hide_hud{}, disable_blur{};
	inline bool show_hud() noexcept {return !hide_hud.load(std::memory_order_relaxed);}
	inline bool blur_disabled() noexcept {return disable_blur.load(std::memory_order_relaxed);}
	inline bool capture_narrative(bool hidden,bool scene_fade) noexcept {return !hidden || scene_fade;}
}
