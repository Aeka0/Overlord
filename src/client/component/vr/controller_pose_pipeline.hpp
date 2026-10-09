#pragma once

#include <atomic>
#include <mutex>

namespace vr::controller_pose_pipeline
{
	enum class mode { standard, legacy };
	inline const char* name(mode value) noexcept
	{
		return value == mode::legacy ? "legacy" : "standard";
	}
	namespace detail
	{
		inline std::atomic<mode> selected{mode::legacy};
		inline std::once_flag startup;
	}

	// One process, one coordinate contract. Saving the calibration choice affects the
	// next game launch; vr_reinit and console edits cannot move held objects.
	inline void initialize(mode value)
	{
		std::call_once(detail::startup, [value] { detail::selected.store(value); });
	}
	inline mode selected() noexcept { return detail::selected.load(); }
}
