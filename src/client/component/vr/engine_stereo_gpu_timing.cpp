#include <std_include.hpp>

#include "engine_stereo_gpu_timing.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>

#include <wrl/client.h>

namespace vr::engine_stereo_gpu_timing
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		struct query_set
		{
			ComPtr<ID3D11Query> disjoint;
			std::array<ComPtr<ID3D11Query>, timestamp_count> timestamps{};
		};

		std::mutex state_mutex;
		std::atomic<capture_state> published_state{capture_state::unprepared};
		report evidence{};
		query_set queries{};
		ComPtr<ID3D11Device> prepared_device;
		ComPtr<ID3D11DeviceContext> prepared_context;
		std::uint8_t expected_marker{};
		bool disjoint_scope_open{};

		[[nodiscard]] constexpr std::uint32_t marker_bit(
			const timestamp_point value) noexcept
		{
			return 1u << static_cast<std::uint32_t>(value);
		}

		[[nodiscard]] bool terminal(const capture_state value) noexcept
		{
			return value == capture_state::complete ||
				value == capture_state::unavailable;
		}

		void publish_state(const capture_state value) noexcept
		{
			evidence.state = value;
			published_state.store(value, std::memory_order_release);
		}

		void release_resources_locked() noexcept
		{
			queries = {};
			prepared_context.Reset();
			prepared_device.Reset();
			disjoint_scope_open = false;
		}

		void close_disjoint_scope_locked() noexcept
		{
			if (!disjoint_scope_open || !prepared_context || !queries.disjoint) return;
			prepared_context->End(queries.disjoint.Get());
			disjoint_scope_open = false;
		}

		void fail_locked(const failure reason, const bool close_scope = true) noexcept
		{
			if (terminal(evidence.state)) return;
			if (close_scope) close_disjoint_scope_locked();
			evidence.error = reason == failure::none ? failure::sequence : reason;
			++evidence.capture_failures;
			publish_state(capture_state::unavailable);
			// A safely closed or never-opened scope has no future readback. Release its
			// COM ownership immediately; an unsafe cross-thread/context failure retains
			// the live scope until the matching device invalidation tears it down.
			if (!disjoint_scope_open) release_resources_locked();
		}

		[[nodiscard]] bool identity_matches_locked(ID3D11DeviceContext* const context,
			const std::uint64_t generation) noexcept
		{
			return context != nullptr && generation != 0 && prepared_context.Get() == context &&
				evidence.context == reinterpret_cast<std::uintptr_t>(context) &&
				evidence.device_generation == generation;
		}

		[[nodiscard]] bool validate_recording_locked(const std::uint64_t pair_id,
			ID3D11DeviceContext* const context, const std::uint64_t generation) noexcept
		{
			if (evidence.state != capture_state::recording) return false;
			if (!identity_matches_locked(context, generation))
			{
				fail_locked(generation != evidence.device_generation ?
					failure::device_generation : failure::context,
					context == prepared_context.Get() &&
					GetCurrentThreadId() == evidence.owner_thread);
				return false;
			}
			if (GetCurrentThreadId() != evidence.owner_thread)
			{
				fail_locked(failure::thread, false);
				return false;
			}
			if (pair_id != evidence.pair_id)
			{
				fail_locked(failure::pair);
				return false;
			}
			return true;
		}

		[[nodiscard]] bool record_marker_locked(const timestamp_point point) noexcept
		{
			const auto index = static_cast<std::uint8_t>(point);
			if (index >= timestamp_count || index != expected_marker ||
				!queries.timestamps[index] || !prepared_context)
			{
				fail_locked(failure::sequence);
				return false;
			}
			prepared_context->End(queries.timestamps[index].Get());
			evidence.marker_mask |= marker_bit(point);
			++expected_marker;
			if (point == timestamp_point::right_conversion_end)
			{
				close_disjoint_scope_locked();
				publish_state(capture_state::pending);
			}
			return true;
		}

		[[nodiscard]] bool mark(const std::uint64_t pair_id, const std::uint32_t eye,
			ID3D11DeviceContext* const context, const std::uint64_t generation,
			const timestamp_point left_point, const timestamp_point right_point) noexcept
		{
			if (published_state.load(std::memory_order_acquire) !=
				capture_state::recording) return false;
			const std::lock_guard lock(state_mutex);
			if (!validate_recording_locked(pair_id, context, generation)) return false;
			if (eye >= 2)
			{
				fail_locked(failure::sequence);
				return false;
			}
			return record_marker_locked(eye == 0 ? left_point : right_point);
		}

		[[nodiscard]] std::uint64_t ticks_to_ns(const std::uint64_t begin,
			const std::uint64_t end, const std::uint64_t frequency) noexcept
		{
			if (end < begin || frequency == 0) return 0;
			const auto ticks = end - begin;
			const long double nanoseconds = static_cast<long double>(ticks) *
				1'000'000'000.0L / static_cast<long double>(frequency);
			if (nanoseconds >= static_cast<long double>(
				(std::numeric_limits<std::uint64_t>::max)()))
			{
				return (std::numeric_limits<std::uint64_t>::max)();
			}
			return nanoseconds > 0.0L ? static_cast<std::uint64_t>(nanoseconds) : 0;
		}

		void calculate_durations_locked() noexcept
		{
			const auto duration = [](const std::size_t begin, const std::size_t end) noexcept
			{
				return ticks_to_ns(evidence.timestamps[begin], evidence.timestamps[end],
					evidence.frequency);
			};
			evidence.left_owner_ns = duration(0, 1);
			evidence.left_conversion_ns = duration(2, 3);
			evidence.right_owner_ns = duration(4, 5);
			evidence.right_conversion_ns = duration(6, 7);
			evidence.owner_to_left_conversion_ns = duration(1, 2);
			evidence.between_eyes_ns = duration(3, 4);
			evidence.owner_to_right_conversion_ns = duration(5, 6);
			evidence.pair_window_ns = duration(0, 7);
		}

	}

	bool prepare_device(ID3D11Device* const device,
		ID3D11DeviceContext* const context, const std::uint64_t device_generation) noexcept
	{
		const auto current = published_state.load(std::memory_order_acquire);
		if (current == capture_state::ready || current == capture_state::recording ||
			current == capture_state::pending || terminal(current))
		{
			if (device == nullptr || context == nullptr || device_generation == 0) return false;
			const std::lock_guard lock(state_mutex);
			return !terminal(evidence.state) && prepared_device.Get() == device &&
				prepared_context.Get() == context &&
				evidence.device_generation == device_generation;
		}

		const std::lock_guard lock(state_mutex);
		if (evidence.state != capture_state::unprepared) return false;
		++evidence.prepare_attempts;
		if (device == nullptr || context == nullptr || device_generation == 0)
		{
			++evidence.prepare_failures;
			evidence.error = failure::invalid_argument;
			publish_state(capture_state::unavailable);
			return false;
		}

		ComPtr<ID3D11Device> context_device;
		context->GetDevice(&context_device);
		if (context_device.Get() != device)
		{
			++evidence.prepare_failures;
			evidence.error = failure::context;
			publish_state(capture_state::unavailable);
			return false;
		}

		query_set replacement{};
		evidence.timestamp_create_results.fill(E_PENDING);
		evidence.timestamp_get_data_results.fill(E_PENDING);
		D3D11_QUERY_DESC description{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
		evidence.disjoint_create_result = device->CreateQuery(&description,
			replacement.disjoint.GetAddressOf());
		if (FAILED(evidence.disjoint_create_result) || !replacement.disjoint)
		{
			++evidence.prepare_failures;
			evidence.error = failure::query_creation;
			publish_state(capture_state::unavailable);
			return false;
		}
		description.Query = D3D11_QUERY_TIMESTAMP;
		for (std::size_t index{}; index < replacement.timestamps.size(); ++index)
		{
			evidence.timestamp_create_results[index] = device->CreateQuery(&description,
				replacement.timestamps[index].GetAddressOf());
			if (FAILED(evidence.timestamp_create_results[index]) ||
				!replacement.timestamps[index])
			{
				++evidence.prepare_failures;
				evidence.error = failure::query_creation;
				publish_state(capture_state::unavailable);
				return false;
			}
		}

		queries = std::move(replacement);
		prepared_device = device;
		prepared_context = context;
		evidence.device = reinterpret_cast<std::uintptr_t>(device);
		evidence.context = reinterpret_cast<std::uintptr_t>(context);
		evidence.device_generation = device_generation;
		evidence.error = failure::none;
		++evidence.prepare_completions;
		publish_state(capture_state::ready);
		return true;
	}

	bool begin_pair(const std::uint64_t pair_id, ID3D11DeviceContext* const context,
		const std::uint64_t device_generation, const bool stable_native_transport) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != capture_state::ready)
			return false;
		const std::lock_guard lock(state_mutex);
		if (evidence.state != capture_state::ready) return false;
		++evidence.eligibility_checks;
		if (!stable_native_transport)
		{
			++evidence.eligibility_skips;
			return false;
		}
		++evidence.capture_attempts;
		if (pair_id == 0 || !identity_matches_locked(context, device_generation))
		{
			fail_locked(pair_id == 0 ? failure::pair :
				(device_generation != evidence.device_generation ?
					failure::device_generation : failure::context), false);
			return false;
		}
		evidence.pair_id = pair_id;
		evidence.owner_thread = GetCurrentThreadId();
		evidence.marker_mask = 0;
		evidence.retired_pair = 0;
		evidence.retirement_observed = false;
		evidence.retirement_polls = 0;
		evidence.not_ready_polls = 0;
		evidence.get_data_calls = 0;
		evidence.get_data_flags = 0;
		evidence.disjoint_get_data_result = E_PENDING;
		evidence.timestamp_get_data_results.fill(E_PENDING);
		evidence.disjoint = false;
		evidence.frequency = 0;
		evidence.timestamps.fill(0);
		evidence.left_owner_ns = 0;
		evidence.left_conversion_ns = 0;
		evidence.right_owner_ns = 0;
		evidence.right_conversion_ns = 0;
		evidence.owner_to_left_conversion_ns = 0;
		evidence.between_eyes_ns = 0;
		evidence.owner_to_right_conversion_ns = 0;
		evidence.pair_window_ns = 0;
		expected_marker = 0;
		prepared_context->Begin(queries.disjoint.Get());
		disjoint_scope_open = true;
		publish_state(capture_state::recording);
		return true;
	}

	bool begin_owner(const std::uint64_t pair_id, const std::uint32_t eye,
		ID3D11DeviceContext* const context, const std::uint64_t generation) noexcept
	{
		return mark(pair_id, eye, context, generation,
			timestamp_point::left_owner_begin, timestamp_point::right_owner_begin);
	}

	bool end_owner(const std::uint64_t pair_id, const std::uint32_t eye,
		ID3D11DeviceContext* const context, const std::uint64_t generation) noexcept
	{
		return mark(pair_id, eye, context, generation,
			timestamp_point::left_owner_end, timestamp_point::right_owner_end);
	}

	bool begin_native_conversion(const std::uint64_t pair_id, const std::uint32_t eye,
		ID3D11DeviceContext* const context, const std::uint64_t generation) noexcept
	{
		return mark(pair_id, eye, context, generation,
			timestamp_point::left_conversion_begin,
			timestamp_point::right_conversion_begin);
	}

	bool end_native_conversion(const std::uint64_t pair_id, const std::uint32_t eye,
		ID3D11DeviceContext* const context, const std::uint64_t generation) noexcept
	{
		return mark(pair_id, eye, context, generation,
			timestamp_point::left_conversion_end,
			timestamp_point::right_conversion_end);
	}

	void finish_pair(const std::uint64_t pair_id, ID3D11DeviceContext* const context,
		const std::uint64_t generation, const bool successfully_published) noexcept
	{
		const auto current = published_state.load(std::memory_order_acquire);
		if (current != capture_state::recording && current != capture_state::pending) return;
		const std::lock_guard lock(state_mutex);
		if (evidence.state != capture_state::recording &&
			evidence.state != capture_state::pending) return;
		if (!identity_matches_locked(context, generation))
		{
			fail_locked(generation != evidence.device_generation ?
				failure::device_generation : failure::context,
				context == prepared_context.Get() &&
				GetCurrentThreadId() == evidence.owner_thread);
			return;
		}
		if (GetCurrentThreadId() != evidence.owner_thread)
		{
			fail_locked(failure::thread, false);
			return;
		}
		if (pair_id != evidence.pair_id)
		{
			fail_locked(failure::pair);
			return;
		}
		if (!successfully_published || evidence.state != capture_state::pending ||
			evidence.marker_mask != (1u << timestamp_count) - 1u)
		{
			fail_locked(failure::incomplete_pair);
		}
	}

	void fail_pair(const std::uint64_t pair_id, ID3D11DeviceContext* const context,
		const std::uint64_t generation, const failure reason) noexcept
	{
		if (published_state.load(std::memory_order_acquire) !=
			capture_state::recording) return;
		const std::lock_guard lock(state_mutex);
		if (evidence.state != capture_state::recording || evidence.pair_id != pair_id) return;
		const auto identity_matches = identity_matches_locked(context, generation);
		const auto thread_matches = GetCurrentThreadId() == evidence.owner_thread;
		fail_locked(!identity_matches ?
			(generation != evidence.device_generation ? failure::device_generation :
				failure::context) : !thread_matches ? failure::thread : reason,
			identity_matches && thread_matches);
	}

	void poll_retired_pair(const std::uint64_t retired_pair_id,
		ID3D11DeviceContext* const context, const std::uint64_t generation) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != capture_state::pending) return;
		const std::lock_guard lock(state_mutex);
		if (evidence.state != capture_state::pending) return;
		if (!identity_matches_locked(context, generation))
		{
			fail_locked(generation != evidence.device_generation ?
				failure::device_generation : failure::context, false);
			return;
		}
		if (GetCurrentThreadId() != evidence.owner_thread)
		{
			fail_locked(failure::thread, false);
			return;
		}
		if (!evidence.retirement_observed)
		{
			if (retired_pair_id != evidence.pair_id) return;
			evidence.retirement_observed = true;
		}
		evidence.retired_pair = retired_pair_id;
		if (++evidence.retirement_polls > maximum_retirement_polls)
		{
			fail_locked(failure::query_timeout, false);
			return;
		}

		constexpr auto flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
		evidence.get_data_flags |= flags;
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data{};
		++evidence.get_data_calls;
		evidence.disjoint_get_data_result = context->GetData(queries.disjoint.Get(),
			&disjoint_data, sizeof(disjoint_data), flags);
		if (evidence.disjoint_get_data_result == S_FALSE)
		{
			++evidence.not_ready_polls;
			return;
		}
		if (FAILED(evidence.disjoint_get_data_result))
		{
			fail_locked(failure::query, false);
			return;
		}
		evidence.disjoint = disjoint_data.Disjoint != FALSE;
		evidence.frequency = disjoint_data.Frequency;
		if (evidence.disjoint || evidence.frequency == 0)
		{
			fail_locked(failure::disjoint, false);
			return;
		}

		for (std::size_t index{}; index < queries.timestamps.size(); ++index)
		{
			++evidence.get_data_calls;
			evidence.timestamp_get_data_results[index] = context->GetData(
				queries.timestamps[index].Get(), &evidence.timestamps[index],
				sizeof(evidence.timestamps[index]), flags);
			if (evidence.timestamp_get_data_results[index] == S_FALSE)
			{
				++evidence.not_ready_polls;
				return;
			}
			if (FAILED(evidence.timestamp_get_data_results[index]))
			{
				fail_locked(failure::query, false);
				return;
			}
		}
		if (!std::is_sorted(evidence.timestamps.begin(), evidence.timestamps.end()))
		{
			fail_locked(failure::timestamp_order, false);
			return;
		}
		calculate_durations_locked();
		evidence.error = failure::none;
		++evidence.capture_completions;
		publish_state(capture_state::complete);
		release_resources_locked();
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t generation) noexcept
	{
		if (published_state.load(std::memory_order_acquire) ==
			capture_state::unprepared) return;
		const std::lock_guard lock(state_mutex);
		if (evidence.state == capture_state::complete) return;
		if (generation != 0 && evidence.device_generation != generation) return;
		if (context != nullptr && prepared_context.Get() != context) return;
		if (evidence.state == capture_state::unavailable)
		{
			release_resources_locked();
			return;
		}
		fail_locked(failure::device_generation,
			evidence.state == capture_state::recording &&
			prepared_context.Get() == context &&
			GetCurrentThreadId() == evidence.owner_thread);
		release_resources_locked();
	}

	report get_report() noexcept
	{
		const std::lock_guard lock(state_mutex);
		auto output = evidence;
		output.state = published_state.load(std::memory_order_acquire);
		return output;
	}

	const char* to_string(const capture_state value) noexcept
	{
		switch (value)
		{
		case capture_state::unprepared: return "unprepared";
		case capture_state::ready: return "ready";
		case capture_state::recording: return "recording";
		case capture_state::pending: return "pending";
		case capture_state::complete: return "complete";
		case capture_state::unavailable: return "unavailable";
		default: return "unknown";
		}
	}

	const char* to_string(const failure value) noexcept
	{
		switch (value)
		{
		case failure::none: return "none";
		case failure::invalid_argument: return "invalid_argument";
		case failure::query_creation: return "query_creation";
		case failure::device_generation: return "device_generation";
		case failure::context: return "context";
		case failure::thread: return "thread";
		case failure::pair: return "pair";
		case failure::sequence: return "sequence";
		case failure::incomplete_pair: return "incomplete_pair";
		case failure::native_ring: return "native_ring";
		case failure::native_conversion: return "native_conversion";
		case failure::query: return "query";
		case failure::query_timeout: return "query_timeout";
		case failure::disjoint: return "disjoint";
		case failure::timestamp_order: return "timestamp_order";
		default: return "unknown";
		}
	}

#ifdef H2VR_GPU_TIMING_TESTING
	void reset_for_tests() noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (disjoint_scope_open) close_disjoint_scope_locked();
		release_resources_locked();
		evidence = {};
		expected_marker = 0;
		published_state.store(capture_state::unprepared, std::memory_order_release);
	}
#endif
}
