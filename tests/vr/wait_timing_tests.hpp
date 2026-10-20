#pragma once
#include "component/vr/diagnostics/wait_timing.hpp"
#include <sstream>
#include <stdexcept>
#include <thread>

namespace wait_timing_tests
{
	namespace timing = vr::diagnostics::wait_timing;
	struct clock
	{
		static inline std::uint64_t tick{}, reads{}, thread_reads{};
		static std::uint64_t now() noexcept { ++reads; return tick; }
		static std::uint32_t thread() noexcept { ++thread_reads; return 77; }
	};
	struct mutex
	{
		bool owned{}, reject{}, published_before_unlock{};
		unsigned locks{}, unlocks{};
		void lock()
		{
			++locks;
			if (reject) throw std::runtime_error("lock failure");
			owned = true;
			clock::tick += 250'000;
		}
		void unlock() noexcept
		{
			timing::snapshot current;
			published_before_unlock = timing::observations[static_cast<std::size_t>(timing::site::hands_solver)].read(current) &&
				current.count != 0;
			owned = false;
			++unlocks;
		}
	};
	template<class Check> void run(Check&& check)
	{
		mutex lock;
		{
			timing::context_scope context({timing::identity::skeleton, 42, 123});
			timing::scoped_lock<mutex, clock> guard(lock, timing::site::hands_solver);
		}
		timing::snapshot value;
		check(lock.locks == 1 && lock.unlocks == 1 && !lock.owned && !clock::reads && !clock::thread_reads,
			"disabled instrumentation preserves locking without reading clock or thread ID");
		check(timing::current_context.kind == timing::identity::none &&
			timing::observations[static_cast<std::size_t>(timing::site::hands_solver)].read(value) && !value.count,
			"disabled instrumentation publishes no context or samples");
		vr::debug_options::selection selected{};
		selected[static_cast<std::size_t>(vr::debug_options::probe::waits)] = true;
		vr::debug_options::initialize(selected);
		vr::debug_options::initialize({});
		check(timing::enabled(), "wait timing uses the frozen startup selection");
		clock::tick = 100;
		{
			timing::context_scope outer({timing::identity::present_frame, 9, 4, 1});
			try
			{
				timing::context_scope context({timing::identity::skeleton, 42, 123});
				timing::scoped_lock<mutex, clock> guard(lock, timing::site::hands_solver);
				clock::tick += 2'000'000;
				throw std::runtime_error("body failure");
			}
			catch (const std::runtime_error&) {}
			check(timing::current_context.kind == timing::identity::present_frame && timing::current_context.id == 9,
				"nested observation identities restore the caller on exception");
		}
		check(!lock.owned && lock.unlocks == 2 && !lock.published_before_unlock,
			"timed lock releases before publishing, including exception unwinding");
		check(timing::observations[static_cast<std::size_t>(timing::site::hands_solver)].read(value) &&
			value.count == 1 && value.latest.started_ns == 100 && value.latest.wait_ns == 250'000 &&
			value.latest.held_ns == 2'000'000 && value.latest.thread == 77 &&
			value.latest.owner.id == 42 && value.latest.owner.object == 123 &&
			value.waits_100us == 1 && !value.waits_1ms && value.holds_1ms == 1,
			"acquisition and held intervals retain separate exact durations and identities");
		lock.reject = true;
		try { timing::scoped_lock<mutex, clock> guard(lock, timing::site::hands_solver); }
		catch (const std::runtime_error&) {}
		check(lock.unlocks == 2 && timing::observations[static_cast<std::size_t>(timing::site::hands_solver)].read(value) &&
			value.count == 1, "failed lock acquisition never unlocks or publishes a held interval");
		{
			timing::measurement<clock> call(timing::site::desktop_present, false);
			clock::tick += 5'000'000;
			call.finish(); call.publish();
			check(timing::observations[static_cast<std::size_t>(timing::site::desktop_present)].read(value) &&
				!value.latest.locking && !value.latest.wait_ns && value.latest.held_ns == 5'000'000,
				"API call duration is not mislabeled as lock acquisition time");
		}
		timing::bucket bounded;
		for (std::uint64_t i = 1; i <= 100; ++i)
			bounded.record({i, i * 100'000, i * 1'000'000, 88, {}, true});
		check(bounded.read(value) && value.count == 100 && value.retained == timing::recent_capacity &&
			value.max_wait.started_ns == 100 && value.max_hold.started_ns == 100 &&
			value.recent[value.next].started_ns == 93,
			"slow history is bounded and maxima survive history rollover");
		timing::bucket concurrent;
		std::array<std::thread, 4> writers;
		for (auto& thread : writers) thread = std::thread([&]
		{
			for (unsigned i{}; i < 500; ++i) concurrent.record({1, 100'000, 2'000'000, 99, {}, true});
		});
		for (auto& thread : writers) thread.join();
		check(concurrent.read(value) && value.count + concurrent.dropped() == 2000 &&
			value.wait_total_ns == value.count * 100'000 && value.held_total_ns == value.count * 2'000'000 &&
			value.retained <= timing::recent_capacity,
			"concurrent producers preserve coherent observations and account for every dropped sample");
		std::ostringstream report;
		timing::append(report);
		check(report.str().find("nested_intervals=inclusive_do_not_sum") != std::string::npos &&
			report.str().find("context=skeleton id=42 object=123") != std::string::npos &&
			report.str().find("dxgi.present snapshot_busy=0 dropped=0 samples=1 type=call") != std::string::npos,
			"report exposes timing semantics and correlated lock/call evidence");
	}
}
