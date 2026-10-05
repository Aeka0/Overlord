#pragma once
#include "region_capture.hpp"

namespace vr::engine_scene_job_capture
{
	// Loader-only installation; never patch the running engine when capture arms.
	void install();
	// Samples known engine-owned memory without following surface pointers.
	// This is a non-atomic observation, not an admission/completion proof.
	void checkpoint(const void* record, std::uintptr_t frontend,
		region_capture::phase point, std::uint64_t family = 0) noexcept;
}
