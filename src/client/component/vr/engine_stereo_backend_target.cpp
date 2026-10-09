#include <std_include.hpp>

#include "engine_stereo_backend_target.hpp"
#include "writable_state.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace vr::engine_stereo_backend_target
{
	namespace
	{
		std::atomic<gate_state> current_state{gate_state::armed};
		std::atomic_uint64_t attempts{};
		std::atomic_uint64_t completions{};
		std::atomic_uint64_t failures{};
		std::atomic_uint64_t select_calls{};
		std::atomic_uint64_t dispatch_select_calls{};
		std::atomic_uint64_t applied_transitions{};
		std::atomic_uint64_t retained_targets{};
		std::atomic_uint64_t invalid_observations{};
		std::atomic_uint64_t application_mismatches{};
		std::atomic_uint64_t entry_mutations{};
		std::atomic_uint64_t route_overflows{};
		std::atomic_uint64_t latest_publication_sequence{};
		std::atomic_uintptr_t latest_record{};
		std::atomic_uint32_t latest_unique_targets{};
		std::mutex report_mutex{};
		H2V_WRITABLE_STATE report published_report{};

		struct frame_write_token
		{
			bool active{};
			bool overflow{};
			std::uint32_t index{};
		};

		std::atomic<gate_state> frame_current_state{gate_state::armed};
		std::atomic_uint64_t frame_attempts{};
		std::atomic_uint64_t frame_completions{};
		std::atomic_uint64_t frame_failures{};
		std::atomic_uint64_t frame_select_calls{};
		std::atomic_uint64_t frame_invalid_observations{};
		std::atomic_uint64_t frame_application_mismatches{};
		std::atomic_uint64_t frame_entry_mutations{};
		std::atomic_uint64_t frame_route_overflows{};
		std::atomic_uint64_t frame_device_generation_mismatches{};
		std::atomic_uint64_t frame_view_copy_events{};
		std::atomic_uint64_t frame_start_publication_sequence{};
		std::atomic_uintptr_t frame_start_record{};
		std::atomic_uint64_t frame_start_present_post_frame{};
		std::atomic_uint32_t frame_start_present_post_thread{};
		std::atomic_uint64_t frame_end_present_pre_frame{};
		std::atomic_uint64_t frame_device_generation{};
		std::atomic_uint32_t frame_boundary_thread_id{};
		std::atomic_uint32_t frame_active_writers{};
		std::atomic_uint32_t frame_maximum_active_writers{};
		std::atomic_uint32_t frame_event_reservations{};
		std::atomic_uint32_t frame_unique_targets{};
		std::atomic_bool frame_finalizer_claimed{};
		std::atomic_uint64_t latest_present_post_frame{};
		std::atomic_uint64_t latest_present_post_generation{};
		std::atomic_uint32_t latest_present_post_thread{};
		std::array<frame_route_event, maximum_frame_route_events> frame_events{};
		std::mutex frame_report_mutex{};
		frame_report published_frame_report{};

		void fail(transaction& active) noexcept
		{
			active.failed = true;
		}

		std::uintptr_t read_backend_state(const void* const context) noexcept
		{
			if (context == nullptr) return 0;
			std::uintptr_t state{};
			std::memcpy(&state, static_cast<const std::uint8_t*>(context) +
				context_state_pointer_offset, sizeof(state));
			return state;
		}

		std::uint32_t read_current_target(const std::uintptr_t state) noexcept
		{
			if (state == 0) return invalid_target;
			std::uint32_t target{};
			std::memcpy(&target, reinterpret_cast<const void*>(state +
				current_target_offset), sizeof(target));
			return target;
		}

		std::uint64_t query_performance_counter() noexcept
		{
			LARGE_INTEGER value{};
			return QueryPerformanceCounter(&value)
				? static_cast<std::uint64_t>(value.QuadPart) : 0;
		}

		route_phase classify_phase(const transaction& active) noexcept
		{
			if (!active) return route_phase::outside_transaction;
			if (active.dispatch_active) return route_phase::dispatch;
			return active.dispatch_returned ? route_phase::after_dispatch :
				route_phase::before_dispatch;
		}

		void update_writer_peak(const std::uint32_t value) noexcept
		{
			auto observed = frame_maximum_active_writers.load(std::memory_order_relaxed);
			while (observed < value && !frame_maximum_active_writers.compare_exchange_weak(
				observed, value, std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		void finalize_frame_observation() noexcept
		{
			if (frame_current_state.load(std::memory_order_acquire) != gate_state::closing ||
				frame_active_writers.load(std::memory_order_acquire) != 0 ||
				frame_finalizer_claimed.exchange(true, std::memory_order_acq_rel))
			{
				return;
			}

			bool invalid_identity{};
			{
				const std::lock_guard lock(frame_report_mutex);
				auto& result = published_frame_report;
				result = {};
				result.start_publication_sequence = frame_start_publication_sequence.load(
					std::memory_order_acquire);
				result.start_record = frame_start_record.load(std::memory_order_acquire);
				result.start_present_post_frame = frame_start_present_post_frame.load(
					std::memory_order_acquire);
				result.start_present_post_thread_id = frame_start_present_post_thread.load(
					std::memory_order_acquire);
				result.end_present_pre_frame = frame_end_present_pre_frame.load(
					std::memory_order_acquire);
				result.device_generation = frame_device_generation.load(std::memory_order_acquire);
				result.boundary_thread_id = frame_boundary_thread_id.load(
					std::memory_order_acquire);
				result.event_count = (std::min)(frame_event_reservations.load(
					std::memory_order_acquire), static_cast<std::uint32_t>(result.events.size()));
				for (std::uint32_t index{}; index < result.event_count; ++index)
				{
					result.events[index] = frame_events[index];
					if (result.events[index].kind == frame_event_kind::view_copy)
					{
						++result.view_copy_event_count;
						continue;
					}
					const auto target_id = result.events[index].target_id;
					if (!result.events[index].entry_valid || target_id >= result.targets.size())
					{
						continue;
					}
					auto& target = result.targets[target_id];
					if (target.seen) continue;
					target.seen = true;
					target.bytes = result.events[index].target_entry;
					++result.unique_target_count;
				}
				frame_unique_targets.store(result.unique_target_count,
					std::memory_order_release);
				invalid_identity = result.start_publication_sequence == 0 ||
					result.start_record == 0 || result.event_count == 0 ||
					result.view_copy_event_count == 0 ||
					result.end_present_pre_frame <= result.start_present_post_frame;
			}

			const auto failed = invalid_identity ||
				frame_invalid_observations.load(std::memory_order_acquire) != 0 ||
				frame_application_mismatches.load(std::memory_order_acquire) != 0 ||
				frame_entry_mutations.load(std::memory_order_acquire) != 0 ||
				frame_route_overflows.load(std::memory_order_acquire) != 0 ||
				frame_device_generation_mismatches.load(std::memory_order_acquire) != 0;
			if (failed)
			{
				frame_failures.fetch_add(1, std::memory_order_relaxed);
				frame_current_state.store(gate_state::failed, std::memory_order_release);
			}
			else
			{
				frame_completions.fetch_add(1, std::memory_order_relaxed);
				frame_current_state.store(gate_state::complete, std::memory_order_release);
			}
		}

		void leave_frame_writer() noexcept
		{
			const auto previous = frame_active_writers.fetch_sub(1, std::memory_order_acq_rel);
			if (previous == 1) finalize_frame_observation();
		}

		frame_write_token begin_frame_event(const transaction& active,
			void* const context, const std::uint32_t target_id,
			const std::uintptr_t caller, const selection_scope& scope,
			const void* const target_registry, const std::uint32_t registry_capacity,
			const bool original_valid) noexcept
		{
			if (frame_current_state.load(std::memory_order_acquire) != gate_state::active)
			{
				return {};
			}
			const auto writers = frame_active_writers.fetch_add(1,
				std::memory_order_acq_rel) + 1;
			update_writer_peak(writers);
			if (frame_current_state.load(std::memory_order_acquire) != gate_state::active)
			{
				leave_frame_writer();
				return {};
			}

			frame_write_token token{true, false,
				frame_event_reservations.fetch_add(1, std::memory_order_relaxed)};
			frame_select_calls.fetch_add(1, std::memory_order_relaxed);
			if (token.index >= frame_events.size())
			{
				token.overflow = true;
				frame_route_overflows.fetch_add(1, std::memory_order_relaxed);
				return token;
			}

			auto& event = frame_events[token.index];
			event = {};
			event.sequence = static_cast<std::uint64_t>(token.index) + 1;
			event.timestamp_qpc = query_performance_counter();
			event.latest_present_post_frame = latest_present_post_frame.load(
				std::memory_order_acquire);
			event.backend_id = scope.backend_id;
			event.publication_sequence = scope.publication_sequence;
			event.context = reinterpret_cast<std::uintptr_t>(context);
			event.backend_state = read_backend_state(context);
			event.caller = caller;
			event.record = scope.record;
			event.thread_id = GetCurrentThreadId();
			event.target_id = target_id;
			event.current_before = read_current_target(event.backend_state);
			event.record_type = scope.record_type;
			event.phase = classify_phase(active);
			event.backend_active = scope.backend_active;
			event.binding_active = scope.binding_active;

			const auto capacity = (std::min)(registry_capacity, target_capacity);
			event.entry_valid = original_valid && target_registry != nullptr &&
				event.backend_state != 0 && target_id < capacity;
			if (event.entry_valid)
			{
				const auto* const entry = static_cast<const std::uint8_t*>(
					target_registry) + static_cast<std::size_t>(target_id) * target_entry_size;
				std::memcpy(event.target_entry.data(), entry, event.target_entry.size());
			}
			else
			{
				frame_invalid_observations.fetch_add(1, std::memory_order_relaxed);
			}
			return token;
		}

		void finish_frame_event(const frame_write_token token,
			const void* const target_registry) noexcept
		{
			if (!token.active) return;
			if (!token.overflow)
			{
				auto& event = frame_events[token.index];
				event.current_after = read_current_target(event.backend_state);
				if (event.entry_valid)
				{
					const auto* const entry = static_cast<const std::uint8_t*>(
						target_registry) + static_cast<std::size_t>(event.target_id) *
							target_entry_size;
					std::array<std::uint8_t, target_entry_size> after{};
					std::memcpy(after.data(), entry, after.size());
					event.entry_stable = after == event.target_entry;
					event.application_matches = event.current_after == event.target_id;
					if (!event.entry_stable)
					{
						frame_entry_mutations.fetch_add(1, std::memory_order_relaxed);
					}
					if (!event.application_matches)
					{
						frame_application_mismatches.fetch_add(1,
							std::memory_order_relaxed);
					}
				}
			}
			leave_frame_writer();
		}

		void begin_frame_observation(const engine_stereo_binding::backend_claim& claim,
			const std::uintptr_t record) noexcept
		{
			if (frame_current_state.load(std::memory_order_acquire) != gate_state::armed)
			{
				return;
			}
			frame_start_publication_sequence.store(claim.publication_sequence,
				std::memory_order_relaxed);
			frame_start_record.store(record, std::memory_order_relaxed);
			frame_start_present_post_frame.store(latest_present_post_frame.load(
				std::memory_order_acquire), std::memory_order_relaxed);
			frame_start_present_post_thread.store(latest_present_post_thread.load(
				std::memory_order_acquire), std::memory_order_relaxed);
			frame_device_generation.store(latest_present_post_generation.load(
				std::memory_order_acquire), std::memory_order_relaxed);
			auto expected = gate_state::armed;
			if (frame_current_state.compare_exchange_strong(expected, gate_state::active,
				std::memory_order_release, std::memory_order_acquire))
			{
				frame_attempts.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	bool begin(transaction& output,
		const engine_stereo_binding::backend_claim& claim,
		const std::uintptr_t record) noexcept
	{
		if (output || !claim || record == 0 || claim.record != record)
		{
			return false;
		}
		auto expected = gate_state::armed;
		if (!current_state.compare_exchange_strong(expected, gate_state::active,
			std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return false;
		}

		attempts.fetch_add(1, std::memory_order_relaxed);
		output.active = true;
		output.evidence.publication_sequence = claim.publication_sequence;
		output.evidence.record = record;
		latest_publication_sequence.store(claim.publication_sequence,
			std::memory_order_release);
		latest_record.store(record, std::memory_order_release);
		begin_frame_observation(claim, record);
		return true;
	}

	void enter_dispatch(transaction& active) noexcept
	{
		if (!active) return;
		if (active.dispatch_active || active.dispatch_entered)
		{
			fail(active);
			return;
		}
		active.dispatch_active = true;
		active.dispatch_entered = true;
	}

	void leave_dispatch(transaction& active) noexcept
	{
		if (!active) return;
		if (!active.dispatch_active || !active.dispatch_entered)
		{
			fail(active);
			return;
		}
		active.dispatch_active = false;
		active.dispatch_returned = true;
	}

	void invoke_select(transaction& active, void* const context,
		const std::uint32_t target_id, const std::uintptr_t caller,
		const selection_scope& scope,
		const void* const target_registry, const std::uint32_t registry_capacity,
		const h2_target_select_fn original) noexcept
	{
		const auto frame_token = begin_frame_event(active, context, target_id, caller,
			scope, target_registry, registry_capacity, original != nullptr);
		if (!active)
		{
			if (original != nullptr) original(context, target_id);
			finish_frame_event(frame_token, target_registry);
			return;
		}

		++active.select_calls;
		select_calls.fetch_add(1, std::memory_order_relaxed);
		if (active.dispatch_active)
		{
			++active.dispatch_select_calls;
			dispatch_select_calls.fetch_add(1, std::memory_order_relaxed);
		}

		route_event event{};
		event.context = reinterpret_cast<std::uintptr_t>(context);
		event.backend_state = read_backend_state(context);
		event.caller = caller;
		event.target_id = target_id;
		event.current_before = read_current_target(event.backend_state);
		event.phase = classify_phase(active);

		const auto capacity = (std::min)(registry_capacity, target_capacity);
		const auto valid = original != nullptr && target_registry != nullptr &&
			event.backend_state != 0 && target_id < capacity;
		std::array<std::uint8_t, target_entry_size> entry_before{};
		if (valid)
		{
			const auto* const entry = static_cast<const std::uint8_t*>(
				target_registry) + static_cast<std::size_t>(target_id) * target_entry_size;
			std::memcpy(entry_before.data(), entry, entry_before.size());
			if (!active.evidence.targets[target_id].seen)
			{
				active.evidence.targets[target_id].seen = true;
				active.evidence.targets[target_id].bytes = entry_before;
				++active.evidence.unique_target_count;
			}
		}
		else
		{
			++active.invalid_observations;
			invalid_observations.fetch_add(1, std::memory_order_relaxed);
			fail(active);
		}

		if (original != nullptr) original(context, target_id);
		event.current_after = read_current_target(event.backend_state);
		if (valid)
		{
			const auto* const entry = static_cast<const std::uint8_t*>(
				target_registry) + static_cast<std::size_t>(target_id) * target_entry_size;
			std::array<std::uint8_t, target_entry_size> entry_after{};
			std::memcpy(entry_after.data(), entry, entry_after.size());
			event.entry_stable = entry_before == entry_after;
			if (!event.entry_stable)
			{
				++active.entry_mutations;
				entry_mutations.fetch_add(1, std::memory_order_relaxed);
				fail(active);
			}
			if (event.current_after != target_id)
			{
				++active.application_mismatches;
				application_mismatches.fetch_add(1, std::memory_order_relaxed);
				fail(active);
			}
			else if (event.current_before == target_id)
			{
				++active.retained_targets;
				retained_targets.fetch_add(1, std::memory_order_relaxed);
			}
			else
			{
				++active.applied_transitions;
				applied_transitions.fetch_add(1, std::memory_order_relaxed);
			}
		}

		if (active.evidence.event_count < active.evidence.events.size())
		{
			active.evidence.events[active.evidence.event_count++] = event;
		}
		else
		{
			++active.route_overflows;
			route_overflows.fetch_add(1, std::memory_order_relaxed);
			fail(active);
		}
		finish_frame_event(frame_token, target_registry);
	}

	void record_view_copy(const transaction& active, const std::uintptr_t caller,
		const selection_scope& scope, const std::uint32_t copy_ordinal,
		const std::uint32_t substitution_ordinal, const std::uint32_t selected_eye,
		const bool source_matched) noexcept
	{
		if (frame_current_state.load(std::memory_order_acquire) != gate_state::active)
		{
			return;
		}
		const auto writers = frame_active_writers.fetch_add(1,
			std::memory_order_acq_rel) + 1;
		update_writer_peak(writers);
		if (frame_current_state.load(std::memory_order_acquire) != gate_state::active)
		{
			leave_frame_writer();
			return;
		}

		const frame_write_token token{true, false,
			frame_event_reservations.fetch_add(1, std::memory_order_relaxed)};
		frame_view_copy_events.fetch_add(1, std::memory_order_relaxed);
		if (token.index >= frame_events.size())
		{
			frame_route_overflows.fetch_add(1, std::memory_order_relaxed);
			leave_frame_writer();
			return;
		}

		auto& event = frame_events[token.index];
		event = {};
		event.sequence = static_cast<std::uint64_t>(token.index) + 1;
		event.timestamp_qpc = query_performance_counter();
		event.latest_present_post_frame = latest_present_post_frame.load(
			std::memory_order_acquire);
		event.backend_id = scope.backend_id;
		event.publication_sequence = scope.publication_sequence;
		event.caller = caller;
		event.record = scope.record;
		event.thread_id = GetCurrentThreadId();
		event.record_type = scope.record_type;
		event.view_copy_ordinal = copy_ordinal;
		event.view_substitution_ordinal = substitution_ordinal;
		event.selected_eye = selected_eye;
		event.kind = frame_event_kind::view_copy;
		event.phase = classify_phase(active);
		event.backend_active = scope.backend_active;
		event.binding_active = scope.binding_active;
		event.view_source_matched = source_matched;
		const auto identity_valid = active && scope.backend_active &&
			scope.binding_active && scope.publication_sequence != 0 &&
			scope.record != 0 && copy_ordinal != 0 &&
			substitution_ordinal <= copy_ordinal && selected_eye < 2;
		if (!identity_valid)
		{
			frame_invalid_observations.fetch_add(1, std::memory_order_relaxed);
		}
		leave_frame_writer();
	}

	void end(transaction& active, const bool command_dispatch_returned) noexcept
	{
		if (!active) return;
		// Runtime evidence proves H2 applies the record's target route before its
		// command stream. A zero dispatch_select_calls count is therefore a valid and
		// important observation, not a failed transaction.
		if (active.dispatch_active || !active.dispatch_entered ||
			!active.dispatch_returned || !command_dispatch_returned ||
			active.select_calls == 0 ||
			active.evidence.unique_target_count == 0)
		{
			fail(active);
		}

		latest_unique_targets.store(active.evidence.unique_target_count,
			std::memory_order_release);
		{
			const std::lock_guard lock(report_mutex);
			published_report = active.evidence;
		}
		if (!active.failed)
		{
			completions.fetch_add(1, std::memory_order_relaxed);
			current_state.store(gate_state::complete, std::memory_order_release);
		}
		else
		{
			failures.fetch_add(1, std::memory_order_relaxed);
			current_state.store(gate_state::failed, std::memory_order_release);
		}
		active = {};
	}

	void on_present_pre(const std::uint64_t frame_index,
		const std::uint64_t device_generation, const std::uint32_t thread_id) noexcept
	{
		if (frame_current_state.load(std::memory_order_acquire) != gate_state::active ||
			frame_index <= frame_start_present_post_frame.load(std::memory_order_acquire))
		{
			return;
		}
		frame_end_present_pre_frame.store(frame_index, std::memory_order_relaxed);
		frame_boundary_thread_id.store(thread_id, std::memory_order_relaxed);
		const auto observed_generation = frame_device_generation.load(
			std::memory_order_acquire);
		if (observed_generation == 0)
		{
			frame_device_generation.store(device_generation, std::memory_order_relaxed);
		}
		else if (device_generation != observed_generation)
		{
			frame_device_generation_mismatches.fetch_add(1,
				std::memory_order_relaxed);
		}

		auto expected = gate_state::active;
		if (!frame_current_state.compare_exchange_strong(expected, gate_state::closing,
			std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return;
		}
		finalize_frame_observation();
	}

	void on_present_post(const std::uint64_t frame_index,
		const std::uint64_t device_generation, const std::uint32_t thread_id,
		const std::int32_t result) noexcept
	{
		if (result < 0) return;
		latest_present_post_generation.store(device_generation, std::memory_order_relaxed);
		latest_present_post_thread.store(thread_id, std::memory_order_relaxed);
		latest_present_post_frame.store(frame_index, std::memory_order_release);
	}

	status get_status() noexcept
	{
		return {
			current_state.load(std::memory_order_acquire),
			attempts.load(std::memory_order_acquire),
			completions.load(std::memory_order_acquire),
			failures.load(std::memory_order_acquire),
			select_calls.load(std::memory_order_acquire),
			dispatch_select_calls.load(std::memory_order_acquire),
			applied_transitions.load(std::memory_order_acquire),
			retained_targets.load(std::memory_order_acquire),
			invalid_observations.load(std::memory_order_acquire),
			application_mismatches.load(std::memory_order_acquire),
			entry_mutations.load(std::memory_order_acquire),
			route_overflows.load(std::memory_order_acquire),
			latest_publication_sequence.load(std::memory_order_acquire),
			latest_record.load(std::memory_order_acquire),
			latest_unique_targets.load(std::memory_order_acquire),
		};
	}

	bool read_report(report& output) noexcept
	{
		const auto state = current_state.load(std::memory_order_acquire);
		if (state != gate_state::complete && state != gate_state::failed) return false;
		const std::lock_guard lock(report_mutex);
		output = published_report;
		return output.event_count != 0;
	}

	frame_status get_frame_status() noexcept
	{
		return {
			frame_current_state.load(std::memory_order_acquire),
			frame_attempts.load(std::memory_order_acquire),
			frame_completions.load(std::memory_order_acquire),
			frame_failures.load(std::memory_order_acquire),
			frame_select_calls.load(std::memory_order_acquire),
			frame_invalid_observations.load(std::memory_order_acquire),
			frame_application_mismatches.load(std::memory_order_acquire),
			frame_entry_mutations.load(std::memory_order_acquire),
			frame_route_overflows.load(std::memory_order_acquire),
			frame_device_generation_mismatches.load(std::memory_order_acquire),
			frame_view_copy_events.load(std::memory_order_acquire),
			frame_start_publication_sequence.load(std::memory_order_acquire),
			frame_start_record.load(std::memory_order_acquire),
			frame_start_present_post_frame.load(std::memory_order_acquire),
			frame_start_present_post_thread.load(std::memory_order_acquire),
			frame_end_present_pre_frame.load(std::memory_order_acquire),
			frame_device_generation.load(std::memory_order_acquire),
			frame_boundary_thread_id.load(std::memory_order_acquire),
			frame_active_writers.load(std::memory_order_acquire),
			frame_maximum_active_writers.load(std::memory_order_acquire),
			frame_unique_targets.load(std::memory_order_acquire),
		};
	}

	bool read_frame_report(frame_report& output) noexcept
	{
		const auto state = frame_current_state.load(std::memory_order_acquire);
		if (state != gate_state::complete && state != gate_state::failed) return false;
		const std::lock_guard lock(frame_report_mutex);
		output = published_frame_report;
		return output.event_count != 0;
	}

	const char* to_string(const gate_state state) noexcept
	{
		switch (state)
		{
		case gate_state::armed: return "armed";
		case gate_state::active: return "active";
		case gate_state::closing: return "closing";
		case gate_state::complete: return "complete";
		case gate_state::failed: return "failed";
		default: return "unknown";
		}
	}

	const char* to_string(const route_phase phase) noexcept
	{
		switch (phase)
		{
		case route_phase::outside_transaction: return "outside_transaction";
		case route_phase::before_dispatch: return "before_dispatch";
		case route_phase::dispatch: return "dispatch";
		case route_phase::after_dispatch: return "after_dispatch";
		default: return "unknown";
		}
	}

	const char* to_string(const frame_event_kind kind) noexcept
	{
		switch (kind)
		{
		case frame_event_kind::target_select: return "target_select";
		case frame_event_kind::view_copy: return "view_copy";
		default: return "unknown";
		}
	}

	bool reset() noexcept
	{
		const auto transaction_state = current_state.load(std::memory_order_acquire);
		const auto frame_state = frame_current_state.load(std::memory_order_acquire);
		if (transaction_state == gate_state::active ||
			frame_state == gate_state::active || frame_state == gate_state::closing ||
			frame_active_writers.load(std::memory_order_acquire) != 0)
		{
			return false;
		}
		{
			const std::lock_guard lock(report_mutex);
			published_report = {};
		}
		{
			const std::lock_guard lock(frame_report_mutex);
			published_frame_report = {};
		}
		for (auto& event : frame_events) event = {};
		attempts.store(0, std::memory_order_relaxed);
		completions.store(0, std::memory_order_relaxed);
		failures.store(0, std::memory_order_relaxed);
		select_calls.store(0, std::memory_order_relaxed);
		dispatch_select_calls.store(0, std::memory_order_relaxed);
		applied_transitions.store(0, std::memory_order_relaxed);
		retained_targets.store(0, std::memory_order_relaxed);
		invalid_observations.store(0, std::memory_order_relaxed);
		application_mismatches.store(0, std::memory_order_relaxed);
		entry_mutations.store(0, std::memory_order_relaxed);
		route_overflows.store(0, std::memory_order_relaxed);
		latest_publication_sequence.store(0, std::memory_order_relaxed);
		latest_record.store(0, std::memory_order_relaxed);
		latest_unique_targets.store(0, std::memory_order_relaxed);
		frame_attempts.store(0, std::memory_order_relaxed);
		frame_completions.store(0, std::memory_order_relaxed);
		frame_failures.store(0, std::memory_order_relaxed);
		frame_select_calls.store(0, std::memory_order_relaxed);
		frame_invalid_observations.store(0, std::memory_order_relaxed);
		frame_application_mismatches.store(0, std::memory_order_relaxed);
		frame_entry_mutations.store(0, std::memory_order_relaxed);
		frame_route_overflows.store(0, std::memory_order_relaxed);
		frame_device_generation_mismatches.store(0, std::memory_order_relaxed);
		frame_view_copy_events.store(0, std::memory_order_relaxed);
		frame_start_publication_sequence.store(0, std::memory_order_relaxed);
		frame_start_record.store(0, std::memory_order_relaxed);
		frame_start_present_post_frame.store(0, std::memory_order_relaxed);
		frame_start_present_post_thread.store(0, std::memory_order_relaxed);
		frame_end_present_pre_frame.store(0, std::memory_order_relaxed);
		frame_device_generation.store(0, std::memory_order_relaxed);
		frame_boundary_thread_id.store(0, std::memory_order_relaxed);
		frame_active_writers.store(0, std::memory_order_relaxed);
		frame_maximum_active_writers.store(0, std::memory_order_relaxed);
		frame_event_reservations.store(0, std::memory_order_relaxed);
		frame_unique_targets.store(0, std::memory_order_relaxed);
		frame_finalizer_claimed.store(false, std::memory_order_relaxed);
		frame_current_state.store(gate_state::armed, std::memory_order_release);
		current_state.store(gate_state::armed, std::memory_order_release);
		return true;
	}
}
