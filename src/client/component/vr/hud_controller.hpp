#pragma once

#include "input_observation.hpp"
#include <atomic>

namespace vr::hud_controller
{
	// Presentation-only identity. Runtime owners publish it; HUD code never
	// calls a runtime or changes gameplay input. Unknown/mixed pairs stay native.
	inline std::atomic_bool openvr_knuckles{}, openxr_knuckles{};
	inline void set_knuckles(controller_input::input_backend backend, bool value) noexcept
	{
		if (backend == controller_input::input_backend::openvr) openvr_knuckles.store(value);
		else if (backend == controller_input::input_backend::openxr) openxr_knuckles.store(value);
	}
	inline bool knuckles(controller_input::input_backend backend) noexcept
	{
		if (backend == controller_input::input_backend::openvr) return openvr_knuckles.load();
		if (backend == controller_input::input_backend::openxr) return openxr_knuckles.load();
		return false;
	}
}
