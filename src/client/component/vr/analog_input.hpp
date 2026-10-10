#pragma once

#include <chrono>
#include <cmath>

namespace vr::controller_input
{
	// The source describes the suggested binding, not a hardware identity.
	enum class analog_source { unknown, value, force, click };
	struct analog_value
	{
		bool active{};
		float value{};
		analog_source source{analog_source::unknown};
	};
	struct analog_policy
	{
		float press{.55f}, release{.45f};
		analog_source source{analog_source::unknown};
		bool operator==(const analog_policy&) const = default;
	};
	inline constexpr analog_policy value_button{.55f, .45f, analog_source::value};
	inline constexpr analog_policy index_trigger{.01f, .005f, analog_source::value};
	inline constexpr analog_policy index_squeeze{.15f, .05f, analog_source::force};
	inline constexpr analog_policy click_button{.5f, .5f, analog_source::click};
	inline constexpr const char* to_string(analog_source source) noexcept
	{
		switch (source)
		{
		case analog_source::value: return "value";
		case analog_source::force: return "force";
		case analog_source::click: return "click";
		default: return "unknown";
		}
	}
	// Runtime latch only; digital_sampler remains the owner of event counters.
	class analog_latch
	{
		using clock = std::chrono::steady_clock;
		clock::time_point at_{};
		analog_policy policy_{};
		bool active_{}, down_{};
	public:
		bool sample(analog_value value, analog_policy policy, clock::time_point now) noexcept
		{
			const bool active = value.active && std::isfinite(value.value) &&
				std::isfinite(policy.press) && std::isfinite(policy.release) &&
				policy.release >= 0 && policy.release <= policy.press && policy.press <= 1;
			if (!active || !active_ || policy != policy_ || now < at_ ||
				now - at_ > std::chrono::milliseconds(150))
				down_ = false;
			if (active)
			{
				if (policy.source == analog_source::click)
					down_ = value.value >= policy.press;
				else if (down_)
					down_ = value.value >= policy.release;
				else
					down_ = value.value >= policy.press;
			}
			active_ = active;
			policy_ = policy;
			at_ = now;
			return down_;
		}
		void reset() noexcept { *this = {}; }
	};
}
