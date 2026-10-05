#pragma once
#include "controller_input.hpp"
#include <algorithm>
#include <cmath>

namespace vr::controller_haptics
{
	struct pulse
	{
		float seconds{}, frequency{}, amplitude{};
		std::uint64_t reference{};
		controller_input::clock::time_point at{};
	};
	// Caller serializes. One pending pulse per physical hand, strongest wins;
	// no unbounded backlog or replay after focus/tracking/recenter changes.
	class mailbox
	{
	public:
		void push(int hand, pulse value) noexcept
		{
			if (hand < 0 || hand > 1 || !std::isfinite(value.seconds) || value.seconds <= 0 ||
				!std::isfinite(value.frequency) || value.frequency <= 0 ||
				!std::isfinite(value.amplitude) || value.amplitude <= 0) return;
			value.seconds = std::min(value.seconds, .1f);
			value.frequency = std::clamp(value.frequency, 1.f, 320.f);
			value.amplitude = std::min(value.amplitude, 1.f);
			auto& old = pending_[hand];
			if (old.reference != value.reference || value.at < old.at ||
				value.at - old.at > std::chrono::milliseconds(100) || value.amplitude >= old.amplitude)
				old = value;
		}
		std::array<pulse, 2> take(const controller_input::frame& input) noexcept
		{
			auto result = pending_;
			pending_ = {};
			for (int h = 0; h < 2; ++h)
				if (!input.focused || !input.sequence || !input.grip[h].valid || !input.aim[h].valid ||
					result[h].reference != input.reference_generation || input.sampled_at < result[h].at ||
					input.sampled_at - result[h].at > std::chrono::milliseconds(100)) result[h] = {};
			return result;
		}
		void clear() noexcept { pending_ = {}; }
	private:
		std::array<pulse, 2> pending_{};
	};
	void request(int hand, pulse value) noexcept;
	std::array<pulse, 2> consume(const controller_input::frame& input) noexcept;
	void clear() noexcept;
	struct status
	{
		std::array<bool, 2> bound{};
		std::array<std::uint64_t, 2> sent{}, failed{};
		std::array<int, 2> last_error{};
	};
	void bindings(std::array<bool, 2> bound) noexcept;
	void delivered(int hand, int runtime_error) noexcept;
	status diagnostics() noexcept;
}
