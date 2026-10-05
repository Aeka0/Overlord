#include <std_include.hpp>
#include "controller_haptics.hpp"
#include <mutex>

namespace vr::controller_haptics
{
	namespace { std::mutex mutex; mailbox pending; status report; }
	void request(int hand, pulse value) noexcept
	{
		const std::lock_guard lock(mutex);
		pending.push(hand, value);
	}
	std::array<pulse, 2> consume(const controller_input::frame& input) noexcept
	{
		const std::lock_guard lock(mutex);
		return pending.take(input);
	}
	void clear() noexcept
	{
		const std::lock_guard lock(mutex);
		pending.clear();
	}
	void bindings(std::array<bool, 2> bound) noexcept
	{
		const std::lock_guard lock(mutex);
		report.bound = bound;
	}
	void delivered(int hand, int runtime_error) noexcept
	{
		if (hand < 0 || hand > 1) return;
		const std::lock_guard lock(mutex);
		++(runtime_error ? report.failed[hand] : report.sent[hand]);
		report.last_error[hand] = runtime_error;
	}
	status diagnostics() noexcept
	{
		const std::lock_guard lock(mutex);
		return report;
	}
}
