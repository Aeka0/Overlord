#pragma once
#include "native_display_contract.hpp"
#include <string>

namespace vr::native_fullscreen_blur
{
	// Inside the native display/GPU ownership scope, after the display transform
	// and before eye HUD composition. Preserves the raw HDR source for H2's tail.
	bool apply(void* record,native_display_contract::route route) noexcept;
	std::string source_status();
}
