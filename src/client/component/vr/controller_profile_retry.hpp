#pragma once
#include <chrono>

namespace vr::controller_profile
{
	// Runtime-owner state: a transient identity query may retry without another
	// device event, but never poll every frame or keep retrying a broken runtime.
	class refresh_retry
	{
	public:
		using clock = std::chrono::steady_clock;
		static constexpr unsigned attempt_limit = 4;
		static constexpr auto retry_delay = std::chrono::milliseconds(250);

		void reset() noexcept { *this = {}; }
		bool ready(clock::time_point now) const noexcept
		{
			return attempts_remaining_ != 0 && now >= next_attempt_;
		}
		void record_result(bool queried, clock::time_point now) noexcept
		{
			if (queried) attempts_remaining_ = 0;
			else if (attempts_remaining_ != 0)
			{
				--attempts_remaining_;
				next_attempt_ = now + retry_delay;
			}
		}

	private:
		unsigned attempts_remaining_{attempt_limit};
		clock::time_point next_attempt_{};
	};
}
