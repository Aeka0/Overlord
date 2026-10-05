#include <std_include.hpp>
#include "component/vr/native_render_contract.hpp"

#include "engine_backend_probe.hpp"
#include "debug_options.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace vr::engine_backend_probe
{
	namespace
	{
		static_assert((frontend_publication_capacity &
			(frontend_publication_capacity - 1)) == 0);
		static_assert((query_publication_capacity &
			(query_publication_capacity - 1)) == 0);
		static_assert((trace_capacity & (trace_capacity - 1)) == 0);
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
			"the backend probe requires lock-free 64-bit atomics");
		static_assert(std::atomic<std::uintptr_t>::is_always_lock_free,
			"the backend probe requires lock-free pointer atomics");
		static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
			"the backend probe requires lock-free 32-bit atomics");

		inline constexpr std::uint64_t trace_domain_shift = 62;
		inline constexpr std::uint64_t trace_sequence_mask =
			(std::uint64_t{1} << trace_domain_shift) - 1;
		inline constexpr std::uintptr_t target_qword_0_offset = 0x00;
		inline constexpr std::uintptr_t target_qword_1_offset = 0x08;
		inline constexpr std::uintptr_t target_qword_2_offset = 0x10;
		inline constexpr std::uintptr_t target_qword_3_offset = 0x18;
		inline constexpr std::uintptr_t target_width_offset = 0x30;
		inline constexpr std::uintptr_t target_height_offset = 0x32;
		inline constexpr std::uintptr_t target_related_offset = 0x38;
		inline constexpr std::uint32_t invalid_record_index =
			(std::numeric_limits<std::uint32_t>::max)();
		inline constexpr std::int32_t success_result = 0;

		enum class query_state : std::uint64_t
		{
			empty,
			pending,
			complete,
			failed,
		};

		enum query_fault : std::uint64_t
		{
			query_fault_none = 0,
			query_fault_null_identity = 1ull << 0,
			query_fault_generation_transition = 1ull << 1,
			query_fault_publication_drop = 1ull << 2,
			query_fault_identity_mismatch = 1ull << 3,
			query_fault_already_resolved = 1ull << 4,
			query_fault_device_mismatch = 1ull << 5,
			query_fault_stale_generation = 1ull << 6,
			query_fault_present_prerequisite = 1ull << 7,
		};

		struct trace_slot
		{
			std::atomic<std::uint64_t> version{};
			std::atomic<std::uint64_t> timestamp_qpc{};
			std::atomic<std::uint64_t> trace_id{};
			std::atomic<std::uint64_t> frontend_epoch{};
			std::atomic<std::uint64_t> metadata{};
			std::array<std::atomic<std::uint64_t>, 8> values{};
		};

		struct frontend_publication_slot
		{
			std::atomic<std::uint64_t> version{};
			std::atomic<std::uint64_t> claimed_backend{};
			std::atomic<std::uintptr_t> record{};
			std::atomic<std::uintptr_t> frontend{};
			std::atomic<std::uint64_t> frontend_epoch{};
			std::atomic<std::uint64_t> frontend_transaction_id{};
			std::atomic<std::uint64_t> metadata{};
		};

		struct query_publication_slot
		{
			std::atomic<std::uint64_t> version{};
			std::atomic<std::uintptr_t> query{};
			std::atomic<std::uint64_t> generation{};
			std::atomic<std::uint64_t> frontend_epoch{};
			std::atomic<std::uint64_t> trace_id{};
			std::atomic<std::uint64_t> device_generation{};
			std::atomic<std::uint64_t> present_frame_index{};
			std::atomic<std::int32_t> present_result{};
			std::atomic<std::uint32_t> publication_thread_id{};
			std::atomic<std::uint64_t> state{};
		};

		struct watchdog_storage
		{
			std::atomic<std::uint64_t> version{};
			std::atomic<std::uint64_t> sequence{};
			std::atomic<std::uint64_t> entered_tick{};
			std::atomic<std::uint64_t> last_progress_tick{};
			std::atomic<std::uint64_t> thread_and_depth{};
			std::atomic<std::uintptr_t> record{};
			std::atomic<std::uintptr_t> command_stream{};
			std::atomic<std::uint32_t> phase{};
		};

		struct thread_present_state
		{
			std::uint64_t probe_generation{};
			std::uint64_t device_generation{};
			std::uint64_t frame_index{};
			std::int32_t result{};
			bool observed{};
		};

		struct thread_watchdog_state
		{
			std::uint64_t probe_generation{};
			std::uint64_t sequence{};
			std::uint32_t depth{};
		};

		struct claimed_frontend
		{
			bool found{};
			std::uint64_t publication_sequence{};
			std::uint64_t frontend_epoch{};
			std::uint64_t frontend_transaction_id{};
			std::uint32_t record_index{invalid_record_index};
			std::uint32_t record_type{};
		};

		struct counters
		{
			std::atomic<std::uint64_t> trace_drops{};

			std::atomic<std::uint64_t> frontend_publications{};
			std::atomic<std::uint64_t> frontend_claims{};
			std::atomic<std::uint64_t> frontend_mapping_misses{};
			std::atomic<std::uint64_t> frontend_publication_drops{};
			std::atomic<std::uint64_t> frontend_unclaimed_overwrites{};

			std::atomic<std::uint64_t> backend_transactions{};
			std::atomic<std::uint64_t> backend_post_binds{};
			std::atomic<std::uint64_t> backend_dispatch_enters{};
			std::atomic<std::uint64_t> backend_cpu_dispatch_returns{};
			std::atomic<std::uint64_t> backend_dispatch_skipped_null{};
			std::atomic<std::uint64_t> backend_incomplete{};
			std::atomic<std::uint64_t> backend_active{};
			std::atomic<std::uint64_t> backend_maximum_active{};
			std::atomic<std::uint64_t> backend_orphan_dispatches{};
			std::atomic<std::uint64_t> backend_record_mismatches{};
			std::atomic<std::uint64_t> backend_thread_mismatches{};
			std::atomic<std::uint64_t> backend_record_type_mismatches{};
			std::atomic<std::uint64_t> backend_target_invalid{};
			std::atomic<std::uint64_t> backend_command_mismatches{};
			std::atomic<std::uint64_t> target_prepare_calls{};
			std::atomic<std::uint64_t> target_prepare_valid_records{};
			std::atomic<std::uint64_t> target_prepare_invalid_records{};
			std::atomic<std::uint64_t> target_prepare_changed_records{};
			std::atomic<std::uint64_t> target_prepare_target_changes{};

			std::atomic<std::uint64_t> query_publications{};
			std::atomic<std::uint64_t> query_publication_drops{};
			std::atomic<std::uint64_t> query_unresolved_overwrites{};
			std::atomic<std::uint64_t> query_results{};
			std::atomic<std::uint64_t> query_pending_results{};
			std::atomic<std::uint64_t> query_complete_results{};
			std::atomic<std::uint64_t> query_failed_results{};
			std::atomic<std::uint64_t> query_identity_mismatches{};
			std::atomic<std::uint64_t> query_generation_mismatches{};
			std::atomic<std::uint64_t> query_device_mismatches{};
			std::atomic<std::uint64_t> query_stale_generations{};
			std::atomic<std::uint64_t> query_present_prerequisite_misses{};
			std::atomic<std::uint64_t> query_unbound_completions{};
			std::atomic<std::uint64_t> present_results{};
			std::atomic<std::uint64_t> last_present_frame_index{};
			std::atomic<std::uint64_t> last_present_device_generation{};
			std::atomic<std::uint32_t> last_present_thread_id{};
			std::atomic<std::int32_t> last_present_result{};

			std::atomic<std::uint64_t> last_backend_id{};
			std::atomic<std::uint64_t> last_frontend_epoch{};
			std::atomic<std::uint64_t> last_frontend_transaction_id{};
			std::atomic<std::uintptr_t> last_record{};
			std::atomic<std::uintptr_t> last_frontend{};
			std::atomic<std::uintptr_t> last_command_stream{};
			std::atomic<std::uint32_t> last_record_index{};
			std::atomic<std::uint32_t> last_record_type{};
			std::atomic<std::uint32_t> last_target_id{};
			std::atomic<std::uint32_t> last_backend_thread_id{};
			std::atomic<std::uint64_t> last_dispatch_duration_qpc{};
			std::atomic<std::uint64_t> last_query_generation{};
			std::atomic<std::uintptr_t> last_query_identity{};
			std::atomic<std::int32_t> last_query_result{};
			std::atomic_bool last_query_complete{};
		};

		std::array<trace_slot, trace_capacity> trace{};
		std::array<frontend_publication_slot, frontend_publication_capacity>
			frontend_publications{};
		std::array<query_publication_slot, query_publication_capacity>
			query_publications{};
		std::atomic_bool probe_enabled{};
		std::atomic<std::uint64_t> probe_generation{1};
		std::atomic<std::uint64_t> trace_sequence{};
		std::atomic<std::uint64_t> frontend_publication_sequence{};
		std::atomic<std::uint64_t> backend_sequence{};
		std::atomic<std::uint64_t> target_prepare_sequence{};
		std::atomic<std::uint64_t> query_publication_sequence{};
		std::atomic<std::uint64_t> query_trace_sequence{};
		std::atomic<std::uint64_t> current_device_generation{};
		std::atomic<std::uint64_t> latest_query_generation{};
		std::atomic<std::uint64_t> observer_callbacks_active{};
		std::atomic<std::uint64_t> watchdog_owner_sequence{};
		std::atomic_int target_registry_state{};
		watchdog_storage watchdog{};
		thread_local thread_present_state thread_present{};
		thread_local thread_watchdog_state thread_watchdog{};
		counters state{};

		class observer_callback_scope final
		{
		public:
			observer_callback_scope() noexcept
			{
				observer_callbacks_active.fetch_add(1, std::memory_order_seq_cst);
			}

			~observer_callback_scope()
			{
				observer_callbacks_active.fetch_sub(1, std::memory_order_seq_cst);
			}

			observer_callback_scope(const observer_callback_scope&) = delete;
			observer_callback_scope& operator=(const observer_callback_scope&) = delete;
		};

		[[nodiscard]] std::uint64_t timestamp_qpc() noexcept
		{
			LARGE_INTEGER value{};
			(void)QueryPerformanceCounter(&value);
			return static_cast<std::uint64_t>(value.QuadPart);
		}

		[[nodiscard]] bool begin_watchdog_write(std::uint64_t& version) noexcept
		{
			version = watchdog.version.load(std::memory_order_acquire);
			return (version & 1) == 0 && watchdog.version.compare_exchange_strong(
				version, version + 1, std::memory_order_acq_rel,
				std::memory_order_acquire);
		}

		void finish_watchdog_write(const std::uint64_t version) noexcept
		{
			watchdog.version.store(version + 2, std::memory_order_release);
		}

		[[nodiscard]] bool publish_watchdog_start(const backend_token& token) noexcept
		{
			std::uint64_t version{};
			if (!begin_watchdog_write(version)) return false;
			watchdog.sequence.store(token.watchdog_sequence, std::memory_order_relaxed);
			const auto now = GetTickCount64();
			watchdog.entered_tick.store(now, std::memory_order_relaxed);
			watchdog.last_progress_tick.store(now, std::memory_order_relaxed);
			watchdog.thread_and_depth.store(pack_u32_pair(token.owner_thread_id,
				thread_watchdog.depth), std::memory_order_relaxed);
			watchdog.record.store(token.record, std::memory_order_relaxed);
			watchdog.command_stream.store(token.command_stream, std::memory_order_relaxed);
			watchdog.phase.store(static_cast<std::uint32_t>(
				backend_watchdog_phase::backend_entered), std::memory_order_relaxed);
			finish_watchdog_write(version);
			return true;
		}

		void update_watchdog(const std::uint64_t sequence, const std::uint32_t depth,
			const std::uintptr_t command_stream,
			const backend_watchdog_phase phase) noexcept
		{
			if (sequence == 0 || watchdog_owner_sequence.load(
				std::memory_order_acquire) != sequence)
			{
				return;
			}
			std::uint64_t version{};
			if (!begin_watchdog_write(version)) return;
			if (watchdog.sequence.load(std::memory_order_relaxed) == sequence)
			{
				const auto thread_id = unpack_low_u32(watchdog.thread_and_depth.load(
					std::memory_order_relaxed));
				if (thread_id == GetCurrentThreadId())
				{
					watchdog.thread_and_depth.store(pack_u32_pair(thread_id, depth),
						std::memory_order_relaxed);
					if (command_stream != 0)
					{
						watchdog.command_stream.store(command_stream,
							std::memory_order_relaxed);
					}
					watchdog.phase.store(static_cast<std::uint32_t>(phase),
						std::memory_order_relaxed);
					watchdog.last_progress_tick.store(GetTickCount64(),
						std::memory_order_relaxed);
				}
			}
			finish_watchdog_write(version);
		}

		void enter_watchdog(backend_token& token) noexcept
		{
			if (thread_watchdog.probe_generation != token.probe_generation)
			{
				thread_watchdog = {};
				thread_watchdog.probe_generation = token.probe_generation;
			}
			++thread_watchdog.depth;
			token.depth = static_cast<std::uint16_t>((std::min<std::uint32_t>)(
				thread_watchdog.depth,
				(std::numeric_limits<std::uint16_t>::max)()));
			if (thread_watchdog.depth == 1)
			{
				std::uint64_t expected{};
				if (watchdog_owner_sequence.compare_exchange_strong(expected,
					token.backend_id, std::memory_order_acq_rel,
					std::memory_order_acquire))
				{
					thread_watchdog.sequence = token.backend_id;
					token.watchdog_sequence = token.backend_id;
					if (!publish_watchdog_start(token))
					{
						(void)watchdog_owner_sequence.compare_exchange_strong(
							token.watchdog_sequence, 0, std::memory_order_release,
							std::memory_order_relaxed);
						thread_watchdog.sequence = 0;
						token.watchdog_sequence = 0;
					}
				}
			}
			else
			{
				token.watchdog_sequence = thread_watchdog.sequence;
				update_watchdog(token.watchdog_sequence, thread_watchdog.depth,
					token.command_stream, backend_watchdog_phase::backend_entered);
			}
		}

		void leave_watchdog(const backend_token& token) noexcept
		{
			if (GetCurrentThreadId() != token.owner_thread_id ||
				thread_watchdog.probe_generation != token.probe_generation ||
				thread_watchdog.depth == 0)
			{
				return;
			}
			--thread_watchdog.depth;
			const auto phase = thread_watchdog.depth == 0
				? backend_watchdog_phase::idle
				: backend_watchdog_phase::backend_entered;
			update_watchdog(token.watchdog_sequence, thread_watchdog.depth,
				token.command_stream, phase);
			if (thread_watchdog.depth == 0 && token.watchdog_sequence != 0)
			{
				auto expected = token.watchdog_sequence;
				(void)watchdog_owner_sequence.compare_exchange_strong(expected, 0,
					std::memory_order_release, std::memory_order_relaxed);
				thread_watchdog.sequence = 0;
			}
		}

		template <typename Value>
		[[nodiscard]] Value read_value(const std::uintptr_t address) noexcept
		{
			Value result{};
			std::memcpy(&result, reinterpret_cast<const void*>(address), sizeof(result));
			return result;
		}

		[[nodiscard]] constexpr std::uint64_t make_trace_id(const trace_domain domain,
			const std::uint64_t sequence) noexcept
		{
			return (static_cast<std::uint64_t>(domain) << trace_domain_shift) |
				(sequence & trace_sequence_mask);
		}

		[[nodiscard]] constexpr trace_domain domain_from_trace_id(
			const std::uint64_t value) noexcept
		{
			return static_cast<trace_domain>(value >> trace_domain_shift);
		}

		[[nodiscard]] constexpr std::uint64_t sequence_from_trace_id(
			const std::uint64_t value) noexcept
		{
			return value & trace_sequence_mask;
		}

		[[nodiscard]] constexpr std::uint64_t pack_metadata(const std::uint32_t thread_id,
			const std::uint16_t depth, const event_kind kind,
			const observation_stage stage) noexcept
		{
			return static_cast<std::uint64_t>(thread_id) |
				(static_cast<std::uint64_t>(depth) << 32) |
				(static_cast<std::uint64_t>(kind) << 48) |
				(static_cast<std::uint64_t>(stage) << 56);
		}

		void unpack_metadata(const std::uint64_t metadata, trace_record& output) noexcept
		{
			output.thread_id = static_cast<std::uint32_t>(metadata);
			output.depth = static_cast<std::uint16_t>(metadata >> 32);
			output.kind = static_cast<event_kind>((metadata >> 48) & 0xFF);
			output.stage = static_cast<observation_stage>(metadata >> 56);
		}

		void publish_trace(const std::uint64_t trace_id,
			const std::uint64_t frontend_epoch, const std::uint16_t depth,
			const event_kind kind, const observation_stage stage,
			const std::array<std::uint64_t, 8>& values) noexcept
		{
			// Detailed trace storage is optional; backend tokens, query observation
			// and the watchdog remain active independently of this event ring.
			if (!debug_options::enabled(debug_options::probe::view)) return;
			if (!probe_enabled.load(std::memory_order_relaxed)) return;
			const auto sequence = trace_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
			auto& slot = trace[(sequence - 1) & (trace_capacity - 1)];
			const auto writing_version = (sequence << 1) | 1;
			auto observed_version = slot.version.load(std::memory_order_acquire);
			if ((observed_version & 1) != 0 || (observed_version >> 1) >= sequence ||
				!slot.version.compare_exchange_strong(observed_version, writing_version,
					std::memory_order_acq_rel, std::memory_order_acquire))
			{
				state.trace_drops.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			slot.timestamp_qpc.store(timestamp_qpc(), std::memory_order_relaxed);
			slot.trace_id.store(trace_id, std::memory_order_relaxed);
			slot.frontend_epoch.store(frontend_epoch, std::memory_order_relaxed);
			slot.metadata.store(pack_metadata(GetCurrentThreadId(), depth, kind, stage),
				std::memory_order_relaxed);
			for (std::size_t index{}; index < values.size(); ++index)
			{
				slot.values[index].store(values[index], std::memory_order_relaxed);
			}
			slot.version.store(sequence << 1, std::memory_order_release);
		}

		void update_maximum(std::atomic<std::uint64_t>& maximum,
			const std::uint64_t candidate) noexcept
		{
			auto observed = maximum.load(std::memory_order_relaxed);
			while (observed < candidate && !maximum.compare_exchange_weak(observed,
				candidate, std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		void decrement_saturating(std::atomic<std::uint64_t>& value) noexcept
		{
			auto observed = value.load(std::memory_order_relaxed);
			while (observed != 0 && !value.compare_exchange_weak(observed, observed - 1,
				std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		void add_fault(backend_token& token, const backend_fault value,
			std::atomic<std::uint64_t>& counter) noexcept
		{
			if (has_fault(token.faults, value)) return;
			token.faults = with_fault(token.faults, value);
			counter.fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] bool readable_protection(const DWORD protection) noexcept
		{
			if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
			switch (protection & 0xFF)
			{
			case PAGE_READONLY:
			case PAGE_READWRITE:
			case PAGE_WRITECOPY:
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] bool readable_range(const std::uintptr_t begin,
			const std::size_t size) noexcept
		{
			if (begin == 0 || size == 0 || begin >
				(std::numeric_limits<std::uintptr_t>::max)() - size)
			{
				return false;
			}
			const auto end = begin + size;
			auto current = begin;
			while (current < end)
			{
				MEMORY_BASIC_INFORMATION information{};
				if (VirtualQuery(reinterpret_cast<const void*>(current), &information,
					sizeof(information)) != sizeof(information) ||
					information.State != MEM_COMMIT ||
					!readable_protection(information.Protect))
				{
					return false;
				}
				const auto region_begin = reinterpret_cast<std::uintptr_t>(
					information.BaseAddress);
				if (region_begin > (std::numeric_limits<std::uintptr_t>::max)() -
					information.RegionSize)
				{
					return false;
				}
				const auto region_end = region_begin + information.RegionSize;
				if (region_end <= current) return false;
				current = (std::min)(region_end, end);
			}
			return true;
		}

		[[nodiscard]] bool target_registry_readable() noexcept
		{
			const auto cached = target_registry_state.load(std::memory_order_acquire);
			if (cached != 0) return cached > 0;
			const auto readable = readable_range(native_render_contract::target_registry_base,
				native_render_contract::target_registry_capacity * native_render_contract::target_registry_stride);
			const auto result = readable ? 1 : -1;
			int expected{};
			(void)target_registry_state.compare_exchange_strong(expected, result,
				std::memory_order_release, std::memory_order_relaxed);
			return target_registry_state.load(std::memory_order_acquire) > 0;
		}

		[[nodiscard]] target_snapshot read_target_snapshot(const std::uintptr_t record,
			const std::uint32_t target_id, backend_token& token) noexcept
		{
			target_snapshot result;
			result.record_target_ids = {
				read_value<std::uint32_t>(record + native_render_contract::record_target_0_offset),
				read_value<std::uint32_t>(record + native_render_contract::record_target_1_offset),
				read_value<std::uint32_t>(record + native_render_contract::record_target_2_offset),
			};
			result.record_pingpong_selector = read_value<std::uint32_t>(
				record + native_render_contract::record_target_selector_offset);
			if (target_id >= native_render_contract::target_registry_capacity)
			{
				add_fault(token, backend_fault::target_invalid, state.backend_target_invalid);
				return result;
			}
			if (!target_registry_readable())
			{
				token.faults = with_fault(token.faults,
					backend_fault::target_registry_unavailable);
				return result;
			}

			result.entry = native_render_contract::target_registry_base +
				static_cast<std::uintptr_t>(target_id) * native_render_contract::target_registry_stride;
			result.qword_0 = read_value<std::uintptr_t>(result.entry + target_qword_0_offset);
			result.qword_1 = read_value<std::uintptr_t>(result.entry + target_qword_1_offset);
			result.qword_2 = read_value<std::uintptr_t>(result.entry + target_qword_2_offset);
			result.qword_3 = read_value<std::uintptr_t>(result.entry + target_qword_3_offset);
			result.width = read_value<std::uint16_t>(result.entry + target_width_offset);
			result.height = read_value<std::uint16_t>(result.entry + target_height_offset);
			result.related_target = read_value<std::uint32_t>(
				result.entry + target_related_offset);
			return result;
		}

		[[nodiscard]] claimed_frontend claim_frontend_publication(
			const std::uintptr_t record, const std::uintptr_t frontend,
			const std::uint64_t backend_trace_id) noexcept
		{
			claimed_frontend result;
			const auto newest = frontend_publication_sequence.load(std::memory_order_acquire);
			const auto first = newest > frontend_publication_capacity
				? newest - frontend_publication_capacity + 1 : 1;
			for (auto sequence = first; sequence <= newest; ++sequence)
			{
				auto& slot = frontend_publications[
					(sequence - 1) & (frontend_publication_capacity - 1)];
				auto expected = sequence << 1;
				if (!slot.version.compare_exchange_strong(expected, expected | 1,
					std::memory_order_acq_rel, std::memory_order_acquire))
				{
					continue;
				}

				const auto slot_record = slot.record.load(std::memory_order_relaxed);
				const auto slot_frontend = slot.frontend.load(std::memory_order_relaxed);
				const auto claimed = slot.claimed_backend.load(std::memory_order_relaxed);
				if (claimed == 0 && slot_record == record && slot_frontend == frontend)
				{
					const auto metadata = slot.metadata.load(std::memory_order_relaxed);
					result.found = true;
					result.publication_sequence = sequence;
					result.frontend_epoch = slot.frontend_epoch.load(
						std::memory_order_relaxed);
					result.frontend_transaction_id = slot.frontend_transaction_id.load(
						std::memory_order_relaxed);
					result.record_index = unpack_low_u32(metadata);
					result.record_type = unpack_high_u32(metadata);
					slot.claimed_backend.store(backend_trace_id, std::memory_order_relaxed);
				}
				slot.version.store(sequence << 1, std::memory_order_release);
				if (result.found) break;
			}
			return result;
		}

		void reset_counter_values() noexcept
		{
			state.trace_drops.store(0, std::memory_order_relaxed);
			state.frontend_publications.store(0, std::memory_order_relaxed);
			state.frontend_claims.store(0, std::memory_order_relaxed);
			state.frontend_mapping_misses.store(0, std::memory_order_relaxed);
			state.frontend_publication_drops.store(0, std::memory_order_relaxed);
			state.frontend_unclaimed_overwrites.store(0, std::memory_order_relaxed);
			state.backend_transactions.store(0, std::memory_order_relaxed);
			state.backend_post_binds.store(0, std::memory_order_relaxed);
			state.backend_dispatch_enters.store(0, std::memory_order_relaxed);
			state.backend_cpu_dispatch_returns.store(0, std::memory_order_relaxed);
			state.backend_dispatch_skipped_null.store(0, std::memory_order_relaxed);
			state.backend_incomplete.store(0, std::memory_order_relaxed);
			state.backend_active.store(0, std::memory_order_relaxed);
			state.backend_maximum_active.store(0, std::memory_order_relaxed);
			state.backend_orphan_dispatches.store(0, std::memory_order_relaxed);
			state.backend_record_mismatches.store(0, std::memory_order_relaxed);
			state.backend_thread_mismatches.store(0, std::memory_order_relaxed);
			state.backend_record_type_mismatches.store(0, std::memory_order_relaxed);
			state.backend_target_invalid.store(0, std::memory_order_relaxed);
			state.backend_command_mismatches.store(0, std::memory_order_relaxed);
			state.target_prepare_calls.store(0, std::memory_order_relaxed);
			state.target_prepare_valid_records.store(0, std::memory_order_relaxed);
			state.target_prepare_invalid_records.store(0, std::memory_order_relaxed);
			state.target_prepare_changed_records.store(0, std::memory_order_relaxed);
			state.target_prepare_target_changes.store(0, std::memory_order_relaxed);
			state.query_publications.store(0, std::memory_order_relaxed);
			state.query_publication_drops.store(0, std::memory_order_relaxed);
			state.query_unresolved_overwrites.store(0, std::memory_order_relaxed);
			state.query_results.store(0, std::memory_order_relaxed);
			state.query_pending_results.store(0, std::memory_order_relaxed);
			state.query_complete_results.store(0, std::memory_order_relaxed);
			state.query_failed_results.store(0, std::memory_order_relaxed);
			state.query_identity_mismatches.store(0, std::memory_order_relaxed);
			state.query_generation_mismatches.store(0, std::memory_order_relaxed);
			state.query_device_mismatches.store(0, std::memory_order_relaxed);
			state.query_stale_generations.store(0, std::memory_order_relaxed);
			state.query_present_prerequisite_misses.store(0,
				std::memory_order_relaxed);
			state.query_unbound_completions.store(0, std::memory_order_relaxed);
			state.present_results.store(0, std::memory_order_relaxed);
			state.last_present_frame_index.store(0, std::memory_order_relaxed);
			state.last_present_device_generation.store(0, std::memory_order_relaxed);
			state.last_present_thread_id.store(0, std::memory_order_relaxed);
			state.last_present_result.store(0, std::memory_order_relaxed);
			state.last_backend_id.store(0, std::memory_order_relaxed);
			state.last_frontend_epoch.store(0, std::memory_order_relaxed);
			state.last_frontend_transaction_id.store(0, std::memory_order_relaxed);
			state.last_record.store(0, std::memory_order_relaxed);
			state.last_frontend.store(0, std::memory_order_relaxed);
			state.last_command_stream.store(0, std::memory_order_relaxed);
			state.last_record_index.store(0, std::memory_order_relaxed);
			state.last_record_type.store(0, std::memory_order_relaxed);
			state.last_target_id.store(0, std::memory_order_relaxed);
			state.last_backend_thread_id.store(0, std::memory_order_relaxed);
			state.last_dispatch_duration_qpc.store(0, std::memory_order_relaxed);
			state.last_query_generation.store(0, std::memory_order_relaxed);
			state.last_query_identity.store(0, std::memory_order_relaxed);
			state.last_query_result.store(0, std::memory_order_relaxed);
			state.last_query_complete.store(false, std::memory_order_relaxed);
		}

		void append_backend_faults(std::ostringstream& output,
			const backend_faults faults)
		{
			if (has_fault(faults, backend_fault::mapping_miss)) output << " mapping_miss";
			if (has_fault(faults, backend_fault::record_mismatch)) output << " record_mismatch";
			if (has_fault(faults, backend_fault::thread_mismatch)) output << " thread_mismatch";
			if (has_fault(faults, backend_fault::record_type_mismatch)) output << " record_type";
			if (has_fault(faults, backend_fault::target_invalid)) output << " target_invalid";
			if (has_fault(faults, backend_fault::orphan_dispatch)) output << " orphan_dispatch";
			if (has_fault(faults, backend_fault::overlap)) output << " overlap";
			if (has_fault(faults, backend_fault::command_mismatch)) output << " command_mismatch";
			if (has_fault(faults, backend_fault::target_registry_unavailable))
				output << " target_registry_unavailable";
		}

		void append_query_faults(std::ostringstream& output,
			const std::uint64_t faults)
		{
			if ((faults & query_fault_null_identity) != 0) output << " null_query";
			if ((faults & query_fault_generation_transition) != 0)
				output << " generation_transition";
			if ((faults & query_fault_publication_drop) != 0)
				output << " publication_drop";
			if ((faults & query_fault_identity_mismatch) != 0)
				output << " identity_mismatch";
			if ((faults & query_fault_already_resolved) != 0)
				output << " already_resolved";
			if ((faults & query_fault_device_mismatch) != 0)
				output << " device_mismatch";
			if ((faults & query_fault_stale_generation) != 0)
				output << " stale_generation";
			if ((faults & query_fault_present_prerequisite) != 0)
				output << " present_prerequisite";
		}
	}

	void set_enabled(const bool enabled) noexcept
	{
		probe_enabled.store(enabled, std::memory_order_seq_cst);
	}

	bool is_enabled() noexcept
	{
		return probe_enabled.load(std::memory_order_acquire);
	}

	bool read_target_registry_entry(const std::uint32_t target_id,
		native_render_contract::target_registry_entry& output) noexcept
	{
		if (target_id >= native_render_contract::target_registry_capacity || !target_registry_readable())
		{
			return false;
		}
		const auto address = native_render_contract::target_registry_base +
			static_cast<std::uintptr_t>(target_id) * native_render_contract::target_registry_stride;
		std::memcpy(output.data(), reinterpret_cast<const void*>(address), output.size());
		return true;
	}

	bool reset() noexcept
	{
		probe_enabled.store(false, std::memory_order_seq_cst);
		constexpr auto quiescence_timeout_ms = std::uint64_t{250};
		const auto started = GetTickCount64();
		while (observer_callbacks_active.load(std::memory_order_seq_cst) != 0 ||
			state.backend_active.load(std::memory_order_acquire) != 0)
		{
			if (GetTickCount64() - started >= quiescence_timeout_ms)
			{
				return false;
			}
			if (!SwitchToThread()) Sleep(1);
		}

		probe_generation.fetch_add(1, std::memory_order_acq_rel);
		trace_sequence.store(0, std::memory_order_relaxed);
		frontend_publication_sequence.store(0, std::memory_order_relaxed);
		// Backend ids also own the watchdog. Keep them monotonic across control-plane
		// resets so a late wrapper from the previous probe generation can never
		// collide with, or clear, a new watchdog owner.
		query_publication_sequence.store(0, std::memory_order_relaxed);
		query_trace_sequence.store(0, std::memory_order_relaxed);
		target_prepare_sequence.store(0, std::memory_order_relaxed);
		// Device generation is external lifecycle state rather than resettable
		// evidence. Keeping this independent atomic also removes any reset race with
		// a device-created callback while observation is disabled.
		latest_query_generation.store(0, std::memory_order_relaxed);
		watchdog_owner_sequence.store(0, std::memory_order_relaxed);
		watchdog.version.store(0, std::memory_order_relaxed);
		watchdog.sequence.store(0, std::memory_order_relaxed);
		watchdog.entered_tick.store(0, std::memory_order_relaxed);
		watchdog.last_progress_tick.store(0, std::memory_order_relaxed);
		watchdog.thread_and_depth.store(0, std::memory_order_relaxed);
		watchdog.record.store(0, std::memory_order_relaxed);
		watchdog.command_stream.store(0, std::memory_order_relaxed);
		watchdog.phase.store(static_cast<std::uint32_t>(
			backend_watchdog_phase::idle), std::memory_order_relaxed);
		reset_counter_values();
		for (auto& slot : trace) slot.version.store(0, std::memory_order_relaxed);
		for (auto& slot : frontend_publications)
		{
			slot.version.store(0, std::memory_order_relaxed);
			slot.claimed_backend.store(0, std::memory_order_relaxed);
		}
		for (auto& slot : query_publications)
		{
			slot.version.store(0, std::memory_order_relaxed);
				slot.state.store(static_cast<std::uint64_t>(query_state::empty),
					std::memory_order_relaxed);
		}
		return true;
	}

	bool publish_frontend_record(const frontend_record_observation& observation) noexcept
	{
		const observer_callback_scope callback_scope;
		if (!probe_enabled.load(std::memory_order_seq_cst) || observation.frontend == 0 ||
			observation.record == 0 || observation.record_index >= 4 ||
			observation.frontend_epoch == 0 || observation.frontend_transaction_id == 0)
		{
			return false;
		}

		const auto sequence = frontend_publication_sequence.fetch_add(
			1, std::memory_order_relaxed) + 1;
		auto& slot = frontend_publications[
			(sequence - 1) & (frontend_publication_capacity - 1)];
		auto observed = slot.version.load(std::memory_order_acquire);
		const auto writing = (sequence << 1) | 1;
		if ((observed & 1) != 0 || (observed >> 1) >= sequence ||
			!slot.version.compare_exchange_strong(observed, writing,
				std::memory_order_acq_rel, std::memory_order_acquire))
		{
			state.frontend_publication_drops.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if ((observed >> 1) != 0 &&
			slot.claimed_backend.load(std::memory_order_relaxed) == 0)
		{
			state.frontend_unclaimed_overwrites.fetch_add(1, std::memory_order_relaxed);
		}

		slot.claimed_backend.store(0, std::memory_order_relaxed);
		slot.record.store(observation.record, std::memory_order_relaxed);
		slot.frontend.store(observation.frontend, std::memory_order_relaxed);
		slot.frontend_epoch.store(observation.frontend_epoch, std::memory_order_relaxed);
		slot.frontend_transaction_id.store(observation.frontend_transaction_id,
			std::memory_order_relaxed);
		slot.metadata.store(pack_u32_pair(observation.record_index,
			observation.record_type), std::memory_order_relaxed);
		slot.version.store(sequence << 1, std::memory_order_release);
		state.frontend_publications.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	backend_token begin_backend(const std::uintptr_t record) noexcept
	{
		const observer_callback_scope callback_scope;
		backend_token token;
		if (!probe_enabled.load(std::memory_order_seq_cst) || record == 0) return token;

		token.backend_id = backend_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
		token.trace_id = make_trace_id(trace_domain::backend, token.backend_id);
		token.probe_generation = probe_generation.load(std::memory_order_acquire);
		token.started_qpc = timestamp_qpc();
		token.record = record;
		token.frontend = read_value<std::uintptr_t>(record + native_render_contract::record_frontend_offset);
		token.command_stream = read_value<std::uintptr_t>(record + native_render_contract::record_command_stream_offset);
		token.record_type = read_value<std::uint32_t>(record + native_render_contract::record_type_offset);
		token.record_index = invalid_record_index;
		token.owner_thread_id = GetCurrentThreadId();

		const auto claimed = claim_frontend_publication(record, token.frontend,
			token.trace_id);
		if (claimed.found)
		{
			token.publication_sequence = claimed.publication_sequence;
			token.frontend_epoch = claimed.frontend_epoch;
			token.frontend_transaction_id = claimed.frontend_transaction_id;
			token.record_index = claimed.record_index;
			state.frontend_claims.fetch_add(1, std::memory_order_relaxed);
			if (claimed.record_type != token.record_type)
			{
				add_fault(token, backend_fault::record_type_mismatch,
					state.backend_record_type_mismatches);
			}
		}
		else
		{
			token.faults = with_fault(token.faults, backend_fault::mapping_miss);
			state.frontend_mapping_misses.fetch_add(1, std::memory_order_relaxed);
		}
		if (token.record_type != native_render_contract::expected_world_record_type)
		{
			add_fault(token, backend_fault::record_type_mismatch,
				state.backend_record_type_mismatches);
		}

		state.backend_transactions.fetch_add(1, std::memory_order_relaxed);
		const auto active = state.backend_active.fetch_add(1,
			std::memory_order_relaxed) + 1;
		if (active > 1)
		{
			token.faults = with_fault(token.faults, backend_fault::overlap);
		}
		update_maximum(state.backend_maximum_active, active);
		enter_watchdog(token);
		state.last_backend_id.store(token.backend_id, std::memory_order_relaxed);
		state.last_frontend_epoch.store(token.frontend_epoch, std::memory_order_relaxed);
		state.last_frontend_transaction_id.store(token.frontend_transaction_id,
			std::memory_order_relaxed);
		state.last_record.store(token.record, std::memory_order_relaxed);
		state.last_frontend.store(token.frontend, std::memory_order_relaxed);
		state.last_command_stream.store(token.command_stream, std::memory_order_relaxed);
		state.last_record_index.store(token.record_index, std::memory_order_relaxed);
		state.last_record_type.store(token.record_type, std::memory_order_relaxed);
		state.last_backend_thread_id.store(token.owner_thread_id,
			std::memory_order_relaxed);

		publish_trace(token.trace_id, token.frontend_epoch, token.depth,
			event_kind::backend_begin, observation_stage::snapshot, {
				token.record,
				token.frontend,
				token.command_stream,
				pack_u32_pair(token.record_index, token.record_type),
				token.frontend_transaction_id,
				token.publication_sequence,
				0,
				token.faults,
			});
		return token;
	}

	void record_post_bind(backend_token& token, const std::uintptr_t record,
		const std::uint32_t target_id) noexcept
	{
		const observer_callback_scope callback_scope;
		if (!token) return;
		if (token.probe_generation != probe_generation.load(std::memory_order_acquire))
		{
			return;
		}
		if (record != token.record)
		{
			add_fault(token, backend_fault::record_mismatch,
				state.backend_record_mismatches);
		}
		if (GetCurrentThreadId() != token.owner_thread_id)
		{
			add_fault(token, backend_fault::thread_mismatch,
				state.backend_thread_mismatches);
		}
		token.target_id = target_id;
		const auto target = read_target_snapshot(token.record, target_id, token);
		update_watchdog(token.watchdog_sequence, thread_watchdog.depth,
			token.command_stream, backend_watchdog_phase::post_bind);
		state.backend_post_binds.fetch_add(1, std::memory_order_relaxed);
		state.last_target_id.store(target_id, std::memory_order_relaxed);

		publish_trace(token.trace_id, token.frontend_epoch, token.depth,
			event_kind::backend_post_bind, observation_stage::snapshot, {
				token.record,
				pack_u32_pair(target_id, target.related_target),
				target.entry,
				target.qword_0,
				target.qword_1,
				target.qword_2,
				target.qword_3,
				token.faults,
			});
		publish_trace(token.trace_id, token.frontend_epoch, token.depth,
			event_kind::backend_target, observation_stage::snapshot, {
				token.record,
				pack_u32_pair(target.width, target.height),
				pack_u32_pair(target.record_target_ids[0], target.record_target_ids[1]),
				target.record_target_ids[2],
				target.record_pingpong_selector,
				token.command_stream,
				token.frontend_transaction_id,
				token.faults,
			});
	}

	void record_dispatch(backend_token& token, const std::uintptr_t commands,
		const observation_stage stage) noexcept
	{
		const observer_callback_scope callback_scope;
		if (!token)
		{
			if (probe_enabled.load(std::memory_order_seq_cst))
			{
				state.backend_orphan_dispatches.fetch_add(1, std::memory_order_relaxed);
				const auto orphan_id = backend_sequence.fetch_add(1,
					std::memory_order_relaxed) + 1;
				publish_trace(make_trace_id(trace_domain::backend, orphan_id), 0, 1,
					event_kind::backend_dispatch, stage, {0, 0, commands, 0, 0, 0, 0,
						fault(backend_fault::orphan_dispatch)});
			}
			return;
		}
		if (token.probe_generation != probe_generation.load(std::memory_order_acquire))
		{
			return;
		}
		if (GetCurrentThreadId() != token.owner_thread_id)
		{
			add_fault(token, backend_fault::thread_mismatch,
				state.backend_thread_mismatches);
		}
		if (commands != token.command_stream)
		{
			add_fault(token, backend_fault::command_mismatch,
				state.backend_command_mismatches);
		}

		std::uint64_t duration{};
		backend_completion completion{backend_completion::none};
		if (stage == observation_stage::before_call)
		{
			token.dispatch_entered = true;
			token.dispatch_started_qpc = timestamp_qpc();
			state.backend_dispatch_enters.fetch_add(1, std::memory_order_relaxed);
			update_watchdog(token.watchdog_sequence, thread_watchdog.depth,
				commands, backend_watchdog_phase::dispatch_before_call);
		}
		else if (stage == observation_stage::after_call)
		{
			token.dispatch_returned = true;
			const auto now = timestamp_qpc();
			duration = token.dispatch_started_qpc != 0 && now >= token.dispatch_started_qpc
				? now - token.dispatch_started_qpc : 0;
			completion = backend_completion::cpu_dispatch_return;
			state.backend_cpu_dispatch_returns.fetch_add(1, std::memory_order_relaxed);
			state.last_dispatch_duration_qpc.store(duration, std::memory_order_relaxed);
			update_watchdog(token.watchdog_sequence, thread_watchdog.depth,
				commands, backend_watchdog_phase::dispatch_after_call);
		}

		publish_trace(token.trace_id, token.frontend_epoch, token.depth,
			event_kind::backend_dispatch, stage, {
				token.record,
				token.command_stream,
				commands,
				token.target_id,
				token.frontend_transaction_id,
				duration,
				static_cast<std::uint64_t>(completion),
				token.faults,
			});
	}

	void end_backend(backend_token& token, const backend_completion completion,
		const std::uintptr_t commands, const backend_faults additional_faults) noexcept
	{
		const observer_callback_scope callback_scope;
		if (!token) return;
		if (token.probe_generation != probe_generation.load(std::memory_order_acquire))
		{
			leave_watchdog(token);
			token = {};
			return;
		}
		token.faults |= additional_faults;
		if (completion == backend_completion::cpu_dispatch_return &&
			!token.dispatch_returned)
		{
			token.faults = with_fault(token.faults, backend_fault::command_mismatch);
			state.backend_command_mismatches.fetch_add(1, std::memory_order_relaxed);
		}
		if (completion == backend_completion::dispatch_skipped_null)
		{
			state.backend_dispatch_skipped_null.fetch_add(1, std::memory_order_relaxed);
		}
		else if (completion == backend_completion::incomplete)
		{
			state.backend_incomplete.fetch_add(1, std::memory_order_relaxed);
		}
		if (commands != 0 && commands != token.command_stream)
		{
			add_fault(token, backend_fault::command_mismatch,
				state.backend_command_mismatches);
		}

		std::uint64_t duration{};
		const auto now = timestamp_qpc();
		if (token.dispatch_started_qpc != 0 && now >= token.dispatch_started_qpc)
		{
			duration = now - token.dispatch_started_qpc;
		}
		publish_trace(token.trace_id, token.frontend_epoch, token.depth,
			event_kind::backend_end, observation_stage::leave, {
				token.record,
				token.command_stream,
				commands,
				token.target_id,
				token.frontend_transaction_id,
				duration,
				static_cast<std::uint64_t>(completion),
				token.faults,
			});

		if (token.probe_generation == probe_generation.load(std::memory_order_acquire))
		{
			decrement_saturating(state.backend_active);
		}
		leave_watchdog(token);
		token = {};
	}

	void record_target_prepare(const target_prepare_observation& observation) noexcept
	{
		const observer_callback_scope callback_scope;
		if (!probe_enabled.load(std::memory_order_seq_cst)) return;
		state.target_prepare_calls.fetch_add(1, std::memory_order_relaxed);
		if (observation.record_valid)
		{
			state.target_prepare_valid_records.fetch_add(1, std::memory_order_relaxed);
		}
		else
		{
			state.target_prepare_invalid_records.fetch_add(1, std::memory_order_relaxed);
		}
		if (observation.changed_bytes != 0)
		{
			state.target_prepare_changed_records.fetch_add(1, std::memory_order_relaxed);
		}
		if (observation.targets_before != observation.targets_after ||
			observation.selector_before != observation.selector_after)
		{
			state.target_prepare_target_changes.fetch_add(1, std::memory_order_relaxed);
		}

		const auto pack_u16_quad = [](const std::uint32_t a, const std::uint32_t b,
			const std::uint32_t c, const std::uint32_t d) noexcept
		{
			return static_cast<std::uint64_t>(a & 0xFFFFu) |
				(static_cast<std::uint64_t>(b & 0xFFFFu) << 16) |
				(static_cast<std::uint64_t>(c & 0xFFFFu) << 32) |
				(static_cast<std::uint64_t>(d & 0xFFFFu) << 48);
		};
		const auto sequence = target_prepare_sequence.fetch_add(1,
			std::memory_order_relaxed) + 1;
		const auto trace_id = make_trace_id(trace_domain::target_prepare, sequence);
		publish_trace(trace_id, 0, 1, event_kind::target_prepare,
			observation_stage::snapshot, {
				observation.caller,
				observation.record,
				observation.frontend,
				pack_u32_pair(observation.record_index, observation.record_type),
				pack_u32_pair(observation.targets_before[0],
					observation.targets_before[1]),
				pack_u32_pair(observation.targets_before[2],
					observation.selector_before),
				observation.record_valid ? 1ull : 0ull,
				0,
			});
		publish_trace(trace_id, 0, 1,
			event_kind::target_prepare, observation_stage::after_call, {
				observation.record,
				observation.frontend,
				pack_u32_pair(observation.record_index, observation.record_type),
				pack_u32_pair(observation.changed_bytes, observation.first_changed),
				pack_u32_pair(observation.last_changed,
					observation.record_valid ? 1u : 0u),
				observation.changed_block_mask,
				static_cast<std::uint64_t>(observation.targets_before[0]) |
					(static_cast<std::uint64_t>(observation.targets_after[0]) << 32) |
					(static_cast<std::uint64_t>(observation.selector_before & 1u) << 62) |
					(static_cast<std::uint64_t>(observation.selector_after & 1u) << 63),
				pack_u16_quad(observation.targets_before[1],
					observation.targets_after[1],
					observation.targets_before[2], observation.targets_after[2]),
			});
	}

	void set_device_generation(const std::uint64_t device_generation) noexcept
	{
		const observer_callback_scope callback_scope;
		const auto previous = current_device_generation.exchange(device_generation,
			std::memory_order_acq_rel);
		if (previous != device_generation)
		{
			// Query generations are scoped to one exact game-device lifetime.  Old
			// slots remain in the trace, but can no longer satisfy a result match.
			latest_query_generation.store(0, std::memory_order_release);
		}
	}

	void record_present_result(const std::uint64_t device_generation,
		const std::uint64_t frame_index, const std::int32_t result) noexcept
	{
		const observer_callback_scope callback_scope;
		if (!probe_enabled.load(std::memory_order_seq_cst)) return;
		const auto generation = probe_generation.load(std::memory_order_acquire);
		thread_present.probe_generation = generation;
		thread_present.device_generation = device_generation;
		thread_present.frame_index = frame_index;
		thread_present.result = result;
		thread_present.observed = true;

		state.present_results.fetch_add(1, std::memory_order_relaxed);
		state.last_present_frame_index.store(frame_index, std::memory_order_relaxed);
		state.last_present_device_generation.store(device_generation,
			std::memory_order_relaxed);
		state.last_present_thread_id.store(GetCurrentThreadId(),
			std::memory_order_relaxed);
		state.last_present_result.store(result, std::memory_order_relaxed);

		const auto query_id = query_trace_sequence.fetch_add(1,
			std::memory_order_relaxed) + 1;
		publish_trace(make_trace_id(trace_domain::query, query_id), 0, 1,
			event_kind::present_result, observation_stage::after_call, {
				device_generation,
				frame_index,
				static_cast<std::uint32_t>(result),
				current_device_generation.load(std::memory_order_acquire),
				0,
				0,
				0,
				0,
			});
	}

	bool record_query_publish(const std::uintptr_t query,
		const std::uint64_t generation_before, const std::uint64_t generation_after,
		const std::uint64_t frontend_epoch) noexcept
	{
		const observer_callback_scope callback_scope;
		if (!probe_enabled.load(std::memory_order_seq_cst)) return false;
		const auto current_probe_generation = probe_generation.load(
			std::memory_order_acquire);
		const auto device_generation = current_device_generation.load(
			std::memory_order_acquire);
		const auto present_matches = thread_present.observed &&
			thread_present.probe_generation == current_probe_generation &&
			device_generation != 0 &&
			thread_present.device_generation == device_generation;
		const auto present_frame_index = present_matches
			? thread_present.frame_index : 0;
		const auto present_result = present_matches
			? thread_present.result : static_cast<std::int32_t>(0x80004005u);
		const auto publication_thread_id = present_matches ? GetCurrentThreadId() : 0;
		std::uint64_t faults{};
		if (query == 0) faults |= query_fault_null_identity;
		const auto valid_transition = generation_before !=
			(std::numeric_limits<std::uint64_t>::max)() &&
			generation_after == generation_before + 1 && generation_after != 0;
		if (!valid_transition)
		{
			faults |= query_fault_generation_transition;
			state.query_generation_mismatches.fetch_add(1, std::memory_order_relaxed);
		}
		if (!present_matches)
		{
			faults |= device_generation == 0 || (thread_present.observed &&
				thread_present.device_generation != device_generation)
				? query_fault_device_mismatch : query_fault_present_prerequisite;
		}
		else if (present_result != success_result)
		{
			faults |= query_fault_present_prerequisite;
		}
		if (valid_transition && device_generation != 0)
		{
			// H2 writes ring[generation_before & 7], then increments its producer.
			// The consumer's EBX is that pre-increment generation, so this—not the
			// counter-after value—is the exact marker identity used by GetData.
			update_maximum(latest_query_generation, generation_before);
		}

		const auto query_id = query_trace_sequence.fetch_add(1,
			std::memory_order_relaxed) + 1;
		const auto trace_id = make_trace_id(trace_domain::query, query_id);
		const auto sequence = query_publication_sequence.fetch_add(1,
			std::memory_order_relaxed) + 1;
		bool published{};
		if (query != 0 && valid_transition)
		{
			auto& slot = query_publications[
				(sequence - 1) & (query_publication_capacity - 1)];
			auto observed = slot.version.load(std::memory_order_acquire);
			const auto writing = (sequence << 1) | 1;
			if ((observed & 1) == 0 && (observed >> 1) < sequence &&
				slot.version.compare_exchange_strong(observed, writing,
					std::memory_order_acq_rel, std::memory_order_acquire))
			{
				if ((observed >> 1) != 0 && static_cast<query_state>(
					slot.state.load(std::memory_order_relaxed)) == query_state::pending)
				{
					state.query_unresolved_overwrites.fetch_add(1,
						std::memory_order_relaxed);
				}
				slot.query.store(query, std::memory_order_relaxed);
				slot.generation.store(generation_before, std::memory_order_relaxed);
				slot.frontend_epoch.store(frontend_epoch, std::memory_order_relaxed);
				slot.trace_id.store(trace_id, std::memory_order_relaxed);
				slot.device_generation.store(device_generation,
					std::memory_order_relaxed);
				slot.present_frame_index.store(present_frame_index,
					std::memory_order_relaxed);
				slot.present_result.store(present_result, std::memory_order_relaxed);
				slot.publication_thread_id.store(publication_thread_id,
					std::memory_order_relaxed);
				slot.state.store(static_cast<std::uint64_t>(query_state::pending),
					std::memory_order_relaxed);
				slot.version.store(sequence << 1, std::memory_order_release);
				state.query_publications.fetch_add(1, std::memory_order_relaxed);
				published = true;
			}
			else
			{
				faults |= query_fault_publication_drop;
				state.query_publication_drops.fetch_add(1, std::memory_order_relaxed);
			}
		}

		publish_trace(trace_id, frontend_epoch, 1, event_kind::query_publish,
			observation_stage::snapshot, {
				query,
				generation_before,
				generation_after,
				device_generation,
				present_frame_index,
				static_cast<std::uint32_t>(present_result),
				sequence | (published ? (1ull << 63) : 0),
				faults,
			});
		return published && valid_transition;
	}

	query_result_class record_query_result(const std::uintptr_t query,
		const std::uint64_t generation, const std::int32_t result,
		const bool complete) noexcept
	{
		const observer_callback_scope callback_scope;
		if (!probe_enabled.load(std::memory_order_seq_cst))
		{
			return query_result_class::identity_mismatch;
		}
		state.query_results.fetch_add(1, std::memory_order_relaxed);
		state.last_query_generation.store(generation, std::memory_order_relaxed);
		state.last_query_identity.store(query, std::memory_order_relaxed);
		state.last_query_result.store(result, std::memory_order_relaxed);
		state.last_query_complete.store(complete, std::memory_order_relaxed);

		const auto newest = query_publication_sequence.load(std::memory_order_acquire);
		const auto first = newest > query_publication_capacity
			? newest - query_publication_capacity + 1 : 1;
		std::uint64_t matched_trace_id{};
		std::uint64_t matched_epoch{};
		std::uint64_t matched_sequence{};
		std::uint64_t matched_device_generation{};
		std::uint64_t matched_present_frame_index{};
		std::int32_t matched_present_result{};
		std::uint32_t matched_publication_thread_id{};
		std::uint64_t faults{};
		bool saw_query_identity{};
		query_result_class classification{query_result_class::identity_mismatch};
		for (auto sequence = newest; sequence >= first && sequence != 0; --sequence)
		{
			auto& slot = query_publications[
				(sequence - 1) & (query_publication_capacity - 1)];
			auto expected = sequence << 1;
			if (!slot.version.compare_exchange_strong(expected, expected | 1,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				continue;
			}

			const auto query_matches = slot.query.load(std::memory_order_relaxed) == query;
			saw_query_identity = saw_query_identity || query_matches;
			const auto identity_matches = query_matches &&
				slot.generation.load(std::memory_order_relaxed) == generation;
			if (identity_matches)
			{
				matched_trace_id = slot.trace_id.load(std::memory_order_relaxed);
				matched_epoch = slot.frontend_epoch.load(std::memory_order_relaxed);
				matched_sequence = sequence;
				matched_device_generation = slot.device_generation.load(
					std::memory_order_relaxed);
				matched_present_frame_index = slot.present_frame_index.load(
					std::memory_order_relaxed);
				matched_present_result = slot.present_result.load(
					std::memory_order_relaxed);
				matched_publication_thread_id = slot.publication_thread_id.load(
					std::memory_order_relaxed);
				const auto previous = static_cast<query_state>(
					slot.state.load(std::memory_order_relaxed));
				const auto active_device_generation = current_device_generation.load(
					std::memory_order_acquire);
				const auto newest_generation = latest_query_generation.load(
					std::memory_order_acquire);
				const auto same_device = active_device_generation != 0 &&
					matched_device_generation == active_device_generation;
				const auto recent_generation = newest_generation >= generation &&
					newest_generation - generation < native_render_contract::h2_query_recent_generation_count;
				const auto successful_prior_present =
					matched_publication_thread_id != 0 &&
					matched_present_result == success_result;
				if (previous != query_state::pending)
				{
					faults |= query_fault_already_resolved;
					classification = query_result_class::identity_mismatch;
				}
				else if (result < 0)
				{
					classification = query_result_class::failed;
					slot.state.store(static_cast<std::uint64_t>(query_state::failed),
						std::memory_order_relaxed);
				}
				else if (!same_device)
				{
					faults |= query_fault_device_mismatch;
					state.query_device_mismatches.fetch_add(1,
						std::memory_order_relaxed);
					classification = query_result_class::identity_mismatch;
				}
				else if (!recent_generation)
				{
					faults |= query_fault_stale_generation;
					state.query_stale_generations.fetch_add(1,
						std::memory_order_relaxed);
					classification = query_result_class::identity_mismatch;
				}
				else if (!successful_prior_present)
				{
					faults |= query_fault_present_prerequisite;
					state.query_present_prerequisite_misses.fetch_add(1,
						std::memory_order_relaxed);
					classification = query_result_class::identity_mismatch;
				}
				else if (result == 0 && complete)
				{
					classification = query_result_class::complete;
					slot.state.store(static_cast<std::uint64_t>(query_state::complete),
						std::memory_order_relaxed);
				}
				else
				{
					// S_FALSE and S_OK/FALSE are both non-terminal.  In particular,
					// S_FALSE can never be promoted to completion by the CPU observer.
					classification = query_result_class::pending;
				}
			}
			slot.version.store(sequence << 1, std::memory_order_release);
			if (identity_matches) break;
		}

		if (classification == query_result_class::identity_mismatch)
		{
			faults |= query_fault_identity_mismatch;
			state.query_identity_mismatches.fetch_add(1, std::memory_order_relaxed);
			if (matched_trace_id == 0 && saw_query_identity)
			{
				state.query_generation_mismatches.fetch_add(1,
					std::memory_order_relaxed);
			}
			if (matched_trace_id == 0)
			{
				const auto query_id = query_trace_sequence.fetch_add(1,
					std::memory_order_relaxed) + 1;
				matched_trace_id = make_trace_id(trace_domain::query, query_id);
			}
		}
		else if (classification == query_result_class::failed)
		{
			state.query_failed_results.fetch_add(1, std::memory_order_relaxed);
		}
		else if (classification == query_result_class::complete)
		{
			state.query_complete_results.fetch_add(1, std::memory_order_relaxed);
			// No backend/resource-generation binding exists in this observer.  A
			// completed query remains unbound evidence and cannot release a target.
			state.query_unbound_completions.fetch_add(1, std::memory_order_relaxed);
		}
		else
		{
			state.query_pending_results.fetch_add(1, std::memory_order_relaxed);
		}

		publish_trace(matched_trace_id, matched_epoch, 1, event_kind::query_result,
			observation_stage::after_call, {
				query,
				generation,
				static_cast<std::uint32_t>(result),
				pack_u32_pair((complete ? 1u : 0u) |
					(static_cast<std::uint32_t>(classification) << 8),
					static_cast<std::uint32_t>(faults)),
				matched_sequence,
				matched_device_generation,
				matched_present_frame_index,
				static_cast<std::uint32_t>(matched_present_result),
			});
		return classification;
	}

	status get_status() noexcept
	{
		status result;
		result.enabled = probe_enabled.load(std::memory_order_acquire);
		result.probe_generation = probe_generation.load(std::memory_order_acquire);
		result.newest_trace_sequence = trace_sequence.load(std::memory_order_acquire);
		result.trace_overwrite_count = result.newest_trace_sequence > trace_capacity
			? result.newest_trace_sequence - trace_capacity : 0;
		result.trace_drop_count = state.trace_drops.load(std::memory_order_relaxed);
		result.frontend_publications = state.frontend_publications.load(std::memory_order_relaxed);
		result.frontend_claims = state.frontend_claims.load(std::memory_order_relaxed);
		result.frontend_mapping_misses = state.frontend_mapping_misses.load(std::memory_order_relaxed);
		result.frontend_publication_drops = state.frontend_publication_drops.load(std::memory_order_relaxed);
		result.frontend_unclaimed_overwrites = state.frontend_unclaimed_overwrites.load(std::memory_order_relaxed);
		result.backend_transactions = state.backend_transactions.load(std::memory_order_relaxed);
		result.backend_post_binds = state.backend_post_binds.load(std::memory_order_relaxed);
		result.backend_dispatch_enters = state.backend_dispatch_enters.load(std::memory_order_relaxed);
		result.backend_cpu_dispatch_returns = state.backend_cpu_dispatch_returns.load(std::memory_order_relaxed);
		result.backend_dispatch_skipped_null = state.backend_dispatch_skipped_null.load(std::memory_order_relaxed);
		result.backend_incomplete = state.backend_incomplete.load(std::memory_order_relaxed);
		result.backend_active = state.backend_active.load(std::memory_order_relaxed);
		result.backend_maximum_active = state.backend_maximum_active.load(std::memory_order_relaxed);
		result.backend_orphan_dispatches = state.backend_orphan_dispatches.load(std::memory_order_relaxed);
		result.backend_record_mismatches = state.backend_record_mismatches.load(std::memory_order_relaxed);
		result.backend_thread_mismatches = state.backend_thread_mismatches.load(std::memory_order_relaxed);
		result.backend_record_type_mismatches = state.backend_record_type_mismatches.load(std::memory_order_relaxed);
		result.backend_target_invalid = state.backend_target_invalid.load(std::memory_order_relaxed);
		result.backend_command_mismatches = state.backend_command_mismatches.load(std::memory_order_relaxed);
		result.target_prepare_calls = state.target_prepare_calls.load(std::memory_order_relaxed);
		result.target_prepare_valid_records = state.target_prepare_valid_records.load(
			std::memory_order_relaxed);
		result.target_prepare_invalid_records = state.target_prepare_invalid_records.load(
			std::memory_order_relaxed);
		result.target_prepare_changed_records = state.target_prepare_changed_records.load(
			std::memory_order_relaxed);
		result.target_prepare_target_changes = state.target_prepare_target_changes.load(
			std::memory_order_relaxed);
		result.query_publications = state.query_publications.load(std::memory_order_relaxed);
		result.query_publication_drops = state.query_publication_drops.load(std::memory_order_relaxed);
		result.query_unresolved_overwrites = state.query_unresolved_overwrites.load(std::memory_order_relaxed);
		result.query_results = state.query_results.load(std::memory_order_relaxed);
		result.query_pending_results = state.query_pending_results.load(std::memory_order_relaxed);
		result.query_complete_results = state.query_complete_results.load(std::memory_order_relaxed);
		result.query_failed_results = state.query_failed_results.load(std::memory_order_relaxed);
		result.query_identity_mismatches = state.query_identity_mismatches.load(std::memory_order_relaxed);
		result.query_generation_mismatches = state.query_generation_mismatches.load(std::memory_order_relaxed);
		result.query_device_mismatches = state.query_device_mismatches.load(
			std::memory_order_relaxed);
		result.query_stale_generations = state.query_stale_generations.load(
			std::memory_order_relaxed);
		result.query_present_prerequisite_misses =
			state.query_present_prerequisite_misses.load(std::memory_order_relaxed);
		result.query_unbound_completions = state.query_unbound_completions.load(std::memory_order_relaxed);
		result.present_results = state.present_results.load(std::memory_order_relaxed);
		result.current_device_generation = current_device_generation.load(
			std::memory_order_acquire);
		result.latest_query_generation = latest_query_generation.load(
			std::memory_order_acquire);
		result.last_present_frame_index = state.last_present_frame_index.load(
			std::memory_order_relaxed);
		result.last_present_device_generation =
			state.last_present_device_generation.load(std::memory_order_relaxed);
		result.last_present_thread_id = state.last_present_thread_id.load(
			std::memory_order_relaxed);
		result.last_present_result = state.last_present_result.load(
			std::memory_order_relaxed);
		result.last_backend_id = state.last_backend_id.load(std::memory_order_relaxed);
		result.last_frontend_epoch = state.last_frontend_epoch.load(std::memory_order_relaxed);
		result.last_frontend_transaction_id = state.last_frontend_transaction_id.load(std::memory_order_relaxed);
		result.last_record = state.last_record.load(std::memory_order_relaxed);
		result.last_frontend = state.last_frontend.load(std::memory_order_relaxed);
		result.last_command_stream = state.last_command_stream.load(std::memory_order_relaxed);
		result.last_record_index = state.last_record_index.load(std::memory_order_relaxed);
		result.last_record_type = state.last_record_type.load(std::memory_order_relaxed);
		result.last_target_id = state.last_target_id.load(std::memory_order_relaxed);
		result.last_backend_thread_id = state.last_backend_thread_id.load(std::memory_order_relaxed);
		result.last_dispatch_duration_qpc = state.last_dispatch_duration_qpc.load(std::memory_order_relaxed);
		result.last_query_generation = state.last_query_generation.load(std::memory_order_relaxed);
		result.last_query_identity = state.last_query_identity.load(std::memory_order_relaxed);
		result.last_query_result = state.last_query_result.load(std::memory_order_relaxed);
		result.last_query_complete = state.last_query_complete.load(std::memory_order_relaxed);
		return result;
	}

	backend_watchdog_status get_backend_watchdog_status() noexcept
	{
		for (std::uint32_t attempt{}; attempt < 4; ++attempt)
		{
			const auto before = watchdog.version.load(std::memory_order_acquire);
			if ((before & 1) != 0) continue;
			backend_watchdog_status result;
			result.sequence = watchdog.sequence.load(std::memory_order_relaxed);
			result.entered_tick = watchdog.entered_tick.load(std::memory_order_relaxed);
			result.last_progress_tick = watchdog.last_progress_tick.load(
				std::memory_order_relaxed);
			const auto thread_and_depth = watchdog.thread_and_depth.load(
				std::memory_order_relaxed);
			result.thread_id = unpack_low_u32(thread_and_depth);
			result.depth = unpack_high_u32(thread_and_depth);
			result.record = watchdog.record.load(std::memory_order_relaxed);
			result.command_stream = watchdog.command_stream.load(
				std::memory_order_relaxed);
			result.phase = static_cast<backend_watchdog_phase>(watchdog.phase.load(
				std::memory_order_relaxed));
			const auto after = watchdog.version.load(std::memory_order_acquire);
			if (before == after) return result;
		}
		return {};
	}

	std::size_t read_recent(trace_record* const output, const std::size_t capacity) noexcept
	{
		if (output == nullptr || capacity == 0) return 0;
		const auto newest = trace_sequence.load(std::memory_order_acquire);
		const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(newest,
			(std::min<std::uint64_t>)(trace_capacity, capacity)));
		const auto first = newest >= count ? newest - count + 1 : 1;
		std::size_t copied{};
		for (std::size_t offset{}; offset < count; ++offset)
		{
			const auto sequence = first + offset;
			const auto& slot = trace[(sequence - 1) & (trace_capacity - 1)];
			const auto before = slot.version.load(std::memory_order_acquire);
			if ((before & 1) != 0 || before != (sequence << 1)) continue;

			trace_record value;
			value.sequence = sequence;
			value.timestamp_qpc = slot.timestamp_qpc.load(std::memory_order_relaxed);
			value.trace_id = slot.trace_id.load(std::memory_order_relaxed);
			value.frontend_epoch = slot.frontend_epoch.load(std::memory_order_relaxed);
			unpack_metadata(slot.metadata.load(std::memory_order_relaxed), value);
			for (std::size_t index{}; index < value.values.size(); ++index)
			{
				value.values[index] = slot.values[index].load(std::memory_order_relaxed);
			}
			const auto after = slot.version.load(std::memory_order_acquire);
			if (before == after) output[copied++] = value;
		}
		return copied;
	}

	std::string format_recent(const std::size_t maximum_records)
	{
		const auto limit = (std::min)(maximum_records, trace_capacity);
		std::vector<trace_record> records(limit);
		const auto count = read_recent(records.data(), records.size());
		const auto snapshot = get_status();
		const auto watchdog_snapshot = get_backend_watchdog_status();
		LARGE_INTEGER frequency{};
		(void)QueryPerformanceFrequency(&frequency);

		std::ostringstream output;
		output << "H2 CPU backend/query trace (newest=" << snapshot.newest_trace_sequence
			<< ", capacity=" << trace_capacity
			<< ", overwritten=" << snapshot.trace_overwrite_count
			<< ", dropped=" << snapshot.trace_drop_count
			<< ", qpc_frequency=" << frequency.QuadPart << ")\r\n";
		output << "completion_contract=cpu_dispatch_return_only gpu_retire_proof=none"
			<< " unbound_query_completions=" << snapshot.query_unbound_completions
			<< " device_generation=" << snapshot.current_device_generation
			<< " latest_query_generation=" << snapshot.latest_query_generation << "\r\n";
		output << "backend_watchdog sequence=" << watchdog_snapshot.sequence
			<< " entered_tick=" << watchdog_snapshot.entered_tick
			<< " last_progress_tick=" << watchdog_snapshot.last_progress_tick
			<< " tid=" << watchdog_snapshot.thread_id
			<< " depth=" << watchdog_snapshot.depth
			<< " record=0x" << std::hex << watchdog_snapshot.record
			<< " commands=0x" << watchdog_snapshot.command_stream << std::dec
			<< " phase=" << to_string(watchdog_snapshot.phase) << "\r\n";
		for (std::size_t index{}; index < count; ++index)
		{
			const auto& value = records[index];
			const auto domain = domain_from_trace_id(value.trace_id);
			const auto* const domain_name = domain == trace_domain::backend ? "backend" :
				(domain == trace_domain::query ? "query" : "target_prepare");
			output << value.sequence << " qpc=" << value.timestamp_qpc
				<< " tid=" << value.thread_id
				<< " frame=" << value.frontend_epoch
				<< " domain=" << domain_name
				<< " token=" << sequence_from_trace_id(value.trace_id)
				<< " depth=" << value.depth << ' ' << to_string(value.kind)
				<< '/' << to_string(value.stage);
			const auto& v = value.values;
			switch (value.kind)
			{
			case event_kind::backend_begin:
				output << " record=0x" << std::hex << v[0]
					<< " frontend=0x" << v[1] << " commands=0x" << v[2]
					<< std::dec << " index=" << unpack_low_u32(v[3])
					<< " type=" << unpack_high_u32(v[3])
					<< " frontend_tx=" << v[4] << " publication=" << v[5]
					<< " faults=0x" << std::hex << v[7] << std::dec;
				append_backend_faults(output, static_cast<backend_faults>(v[7]));
				break;
			case event_kind::backend_post_bind:
				output << " record=0x" << std::hex << v[0] << std::dec
					<< " target=" << unpack_low_u32(v[1])
					<< " related=" << unpack_high_u32(v[1])
					<< " entry=0x" << std::hex << v[2]
					<< " qword0=0x" << v[3] << " qword1=0x" << v[4]
					<< " qword2=0x" << v[5] << " qword3=0x" << v[6] << std::dec
					<< " faults=0x" << std::hex << v[7] << std::dec;
				append_backend_faults(output, static_cast<backend_faults>(v[7]));
				break;
			case event_kind::backend_target:
				output << " record=0x" << std::hex << v[0] << std::dec
					<< " size=" << unpack_low_u32(v[1]) << 'x' << unpack_high_u32(v[1])
					<< " record_target_ids=" << unpack_low_u32(v[2]) << ','
					<< unpack_high_u32(v[2]) << ',' << v[3]
					<< " pingpong_selector=" << v[4] << " commands=0x" << std::hex << v[5]
					<< std::dec << " frontend_tx=" << v[6] << " faults=0x" << std::hex << v[7]
					<< std::dec;
				append_backend_faults(output, static_cast<backend_faults>(v[7]));
				break;
			case event_kind::backend_dispatch:
			case event_kind::backend_end:
				output << " record=0x" << std::hex << v[0]
					<< " expected_commands=0x" << v[1] << " commands=0x" << v[2]
					<< std::dec << " target=" << v[3] << " frontend_tx=" << v[4]
					<< " duration_qpc=" << v[5] << " completion="
					<< to_string(static_cast<backend_completion>(v[6]))
					<< " faults=0x" << std::hex << v[7] << std::dec;
				append_backend_faults(output, static_cast<backend_faults>(v[7]));
				break;
			case event_kind::query_publish:
				output << " query=0x" << std::hex << v[0] << std::dec
					<< " generation=" << v[1] << "->" << v[2]
					<< " device_generation=" << v[3]
					<< " present_frame=" << v[4] << " present_result=0x"
					<< std::hex << static_cast<std::uint32_t>(v[5]) << std::dec
					<< " publication=" << (v[6] & ~(1ull << 63))
					<< " stored=" << ((v[6] >> 63) != 0 ? "yes" : "no")
					<< " faults=0x" << std::hex << v[7] << std::dec;
				append_query_faults(output, v[7]);
				break;
			case event_kind::query_result:
			{
				const auto result_metadata = unpack_low_u32(v[3]);
				const auto query_faults = unpack_high_u32(v[3]);
				output << " query=0x" << std::hex << v[0] << std::dec
					<< " generation=" << v[1] << " result=0x" << std::hex
					<< static_cast<std::uint32_t>(v[2]) << std::dec
					<< " complete=" << ((result_metadata & 1) != 0 ? "yes" : "no")
					<< " class=" << to_string(static_cast<query_result_class>(
						(result_metadata >> 8) & 0xFF))
					<< " publication=" << v[4]
					<< " device_generation=" << v[5]
					<< " present_frame=" << v[6] << " present_result=0x"
					<< std::hex << static_cast<std::uint32_t>(v[7])
					<< " faults=0x" << query_faults << std::dec;
				append_query_faults(output, query_faults);
				break;
			}
			case event_kind::present_result:
				output << " device_generation=" << v[0] << " frame=" << v[1]
					<< " result=0x" << std::hex << static_cast<std::uint32_t>(v[2])
					<< std::dec << " active_device_generation=" << v[3];
				break;
			case event_kind::target_prepare:
			{
				if (value.stage == observation_stage::snapshot)
				{
					output << " caller=0x" << std::hex << v[0]
						<< " record=0x" << v[1] << " frontend=0x" << v[2]
						<< std::dec << " index=" << unpack_low_u32(v[3])
						<< " type=" << unpack_high_u32(v[3])
						<< " targets_before=" << unpack_low_u32(v[4]) << ','
						<< unpack_high_u32(v[4]) << ',' << unpack_low_u32(v[5])
						<< " selector_before=" << unpack_high_u32(v[5])
						<< " record_valid=" << (v[6] != 0 ? "yes" : "no");
					break;
				}
				const auto packed = v[7];
				output << " record=0x" << std::hex << v[0]
					<< " frontend=0x" << v[1] << std::dec
					<< " index=" << unpack_low_u32(v[2])
					<< " type=" << unpack_high_u32(v[2])
					<< " changed_bytes=" << unpack_low_u32(v[3])
					<< " changed_range=" << unpack_high_u32(v[3]) << ".."
					<< unpack_low_u32(v[4])
					<< " record_valid=" << (unpack_high_u32(v[4]) != 0 ? "yes" : "no")
					<< " changed_blocks=0x" << std::hex << v[5] << std::dec
					<< " target0=" << unpack_low_u32(v[6]) << "->"
					<< (unpack_high_u32(v[6]) & 0x3FFFFFFFu)
					<< " target1=" << static_cast<std::uint16_t>(packed) << "->"
					<< static_cast<std::uint16_t>(packed >> 16)
					<< " target2=" << static_cast<std::uint16_t>(packed >> 32) << "->"
					<< static_cast<std::uint16_t>(packed >> 48)
					<< " selector=" << ((v[6] >> 62) & 1) << "->"
					<< ((v[6] >> 63) & 1);
				break;
			}
			default:
				break;
			}
			output << "\r\n";
		}
		return output.str();
	}

	const char* to_string(const event_kind value) noexcept
	{
		switch (value)
		{
		case event_kind::backend_begin: return "backend_begin";
		case event_kind::backend_post_bind: return "backend_post_bind";
		case event_kind::backend_target: return "backend_target";
		case event_kind::backend_dispatch: return "backend_dispatch";
		case event_kind::backend_end: return "backend_end";
		case event_kind::query_publish: return "query_publish";
		case event_kind::query_result: return "query_result";
		case event_kind::present_result: return "present_result";
		case event_kind::target_prepare: return "target_prepare";
		default: return "unknown";
		}
	}

	const char* to_string(const observation_stage value) noexcept
	{
		switch (value)
		{
		case observation_stage::snapshot: return "snapshot";
		case observation_stage::before_call: return "before_call";
		case observation_stage::after_call: return "after_call";
		case observation_stage::leave: return "leave";
		default: return "unknown";
		}
	}

	const char* to_string(const backend_completion value) noexcept
	{
		switch (value)
		{
		case backend_completion::none: return "none";
		case backend_completion::cpu_dispatch_return: return "cpu_dispatch_return";
		case backend_completion::dispatch_skipped_null: return "dispatch_skipped_null";
		case backend_completion::incomplete: return "incomplete";
		default: return "unknown";
		}
	}

	const char* to_string(const backend_watchdog_phase value) noexcept
	{
		switch (value)
		{
		case backend_watchdog_phase::idle: return "idle";
		case backend_watchdog_phase::backend_entered: return "backend_entered";
		case backend_watchdog_phase::post_bind: return "post_bind";
		case backend_watchdog_phase::dispatch_before_call: return "dispatch_before";
		case backend_watchdog_phase::dispatch_after_call: return "dispatch_after";
		case backend_watchdog_phase::leaving: return "leaving";
		default: return "unknown";
		}
	}

	const char* to_string(const query_result_class value) noexcept
	{
		switch (value)
		{
		case query_result_class::identity_mismatch: return "identity_mismatch";
		case query_result_class::pending: return "pending";
		case query_result_class::complete: return "complete_unbound";
		case query_result_class::failed: return "failed";
		default: return "unknown";
		}
	}
}
