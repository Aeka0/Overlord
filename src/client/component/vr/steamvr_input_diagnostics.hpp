#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace vr::steamvr_input
{
	struct input_event
	{
		std::chrono::steady_clock::time_point at{};
		std::uint64_t initialization{};
		std::uint32_t type{}, device{};
		std::array<std::uint64_t, 4> details{};
	};

	// Immutable text is built only at initialization / bounded event probes.
	// Publishing runtime status copies pointers, never formats or reads files.
	struct diagnostic_snapshot
	{
		std::uint64_t initializations{}, resets{}, probes{}, probes_skipped{}, collection_failures{}, events_discarded{};
		std::shared_ptr<const std::string> first_setup, latest_setup, first_failed_setup, first_probe, latest_probe, first_failed_probe;
		std::array<input_event, 8> events{};
		std::size_t event_next{}, event_count{};
		input_event first_load_failure{};
	};
}
