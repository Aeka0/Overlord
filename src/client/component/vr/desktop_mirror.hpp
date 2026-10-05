#pragma once

#include <cstdint>

namespace vr::desktop_mirror
{
	enum class state : std::uint8_t { idle, ui, waiting_eye, invalid_view, wrong_target, failed, drawing };
	struct report
	{
		state phase{state::idle};
		std::uint64_t draws{}, last_pair{};
		std::int32_t last_result{};
		float requested_fov{}, effective_fov{};
		bool fov_limited{};
		float smoothing_strength{},correction_fraction{1};
	};
	// Called only inside the existing GUI owner callback. No independent render
	// loop, scene camera, shared texture, GPU wait, or OpenVR call is introduced.
	void draw(float horizontal_fov);
	report get_report() noexcept;
	const char* to_string(state value) noexcept;
}
