#include <std_include.hpp>

#include "engine_stereo_execution.hpp"

#include "engine_stereo_draw_indexed.hpp"
#include "native_conversion_command_list.hpp"

#include "utils/hook.hpp"
#include "utils/hook_validation.hpp"

#include <atomic>
#include <cstring>
#include <mutex>

namespace vr::engine_stereo_execution
{
	namespace
	{
		constexpr std::size_t draw_indexed_slot = 12;
		constexpr std::size_t output_merger_slot = 33;
		constexpr std::size_t output_merger_unordered_access_slot = 34;
		constexpr std::size_t draw_slot = 13;
		constexpr std::size_t draw_indexed_instanced_slot = 20;
		constexpr std::size_t draw_instanced_slot = 21;
		constexpr std::size_t draw_auto_slot = 38;
		constexpr std::size_t draw_indexed_instanced_indirect_slot = 39;
		constexpr std::size_t draw_instanced_indirect_slot = 40;
		constexpr std::size_t dispatch_slot = 41;
		constexpr std::size_t dispatch_indirect_slot = 42;
		constexpr std::size_t execute_command_list_slot = 58;
		constexpr std::size_t clear_state_slot = 110;
		constexpr std::uint32_t observer_hook_count = 9;
		constexpr std::size_t maximum_unique_identities = 256;
		constexpr std::size_t maximum_call_stack_keys = 512;
		constexpr std::uint64_t admission_open_bit = 1ull << 63;
		constexpr std::uint64_t admission_frame_bit = 1ull << 62;
		constexpr std::uint64_t admission_writer_mask = 0xFFFFFFFFull;

		using draw_fn = void(__stdcall*)(ID3D11DeviceContext*, UINT, UINT);
		using draw_indexed_instanced_fn = void(__stdcall*)(ID3D11DeviceContext*,
			UINT, UINT, UINT, INT, UINT);
		using draw_instanced_fn = void(__stdcall*)(ID3D11DeviceContext*, UINT,
			UINT, UINT, UINT);
		using draw_auto_fn = void(__stdcall*)(ID3D11DeviceContext*);
		using indirect_fn = void(__stdcall*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
		using dispatch_fn = void(__stdcall*)(ID3D11DeviceContext*, UINT, UINT, UINT);
		using execute_command_list_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11CommandList*, BOOL);

		utils::hook::detour draw_hook{};
		std::array<std::atomic<invocation_observer_fn>,
			invocation_observer_channel_count> invocation_observers{};
		std::atomic<opaque_state_observer_fn> opaque_state_observer{};
		std::atomic<draw_indexed_observer_fn> draw_indexed_observer{};
		std::mutex observer_registration_mutex;
		std::atomic_uint32_t observer_mask{};
		constexpr auto invocation_mask = (1u << invocation_observer_channel_count) - 1;
		constexpr auto opaque_mask = 1u << invocation_observer_channel_count;
		constexpr auto indexed_mask = opaque_mask << 1;
		utils::hook::detour draw_indexed_instanced_hook{};
		utils::hook::detour draw_instanced_hook{};
		utils::hook::detour draw_auto_hook{};
		utils::hook::detour draw_indexed_instanced_indirect_hook{};
		utils::hook::detour draw_instanced_indirect_hook{};
		utils::hook::detour dispatch_hook{};
		utils::hook::detour dispatch_indirect_hook{};
		utils::hook::detour execute_command_list_hook{};
		std::mutex hook_mutex{};
		std::mutex report_mutex{};
		std::atomic_bool hooks_installed{};
		std::atomic_bool installation_permanently_failed{};
		std::atomic_bool draw_indexed_forwarding_verified{};
		std::atomic_bool targets_distinct{};
		std::atomic_uint32_t installed_hook_count{};
		std::array<std::atomic_uintptr_t, api_count> hook_targets{};
		std::atomic_uintptr_t output_merger_target{};
		std::atomic_uintptr_t output_merger_unordered_access_target{};
		std::atomic_uintptr_t clear_state_target{};
		std::atomic_bool deferred_context_probe_attempted{};
		std::atomic_bool deferred_context_probe_succeeded{};
		std::array<std::atomic_bool, api_count> deferred_target_matches{};
		std::atomic_bool deferred_output_merger_target_matches{};
		std::atomic_uintptr_t expected_context{};
		std::atomic_uint64_t expected_generation{};
		std::atomic_uintptr_t observation_context{};
		std::atomic_uint64_t observation_generation{};
		std::atomic_uint64_t hook_failures{};

		std::atomic<gate_state> current_state{gate_state::classifier_pending};
		std::atomic<failure> current_failure{failure::none};
		std::atomic_bool failure_requested{};
		std::atomic_uint64_t attempts{};
		std::atomic_uint64_t completions{};
		std::atomic_uint64_t failures{};
		std::atomic_uint32_t event_reservations{};
		std::atomic_uint32_t recorded_events{};
		std::atomic_uint32_t classifier_events{};
		std::atomic_uint32_t frame_events{};
		std::atomic_uint32_t overflow_count{};
		std::atomic_uint32_t foreign_context_events{};
		std::atomic_uint32_t expected_context_frame_events{};
		std::atomic_uint32_t backend_scoped_events{};
		std::atomic_uint32_t backend_scoped_frame_events{};
		std::atomic_uint32_t backend_unscoped_frame_events{};
		std::atomic_uint32_t backend_thread_mismatches{};
		std::atomic_uint32_t distinct_backend_records{};
		std::array<std::atomic_uintptr_t, maximum_unique_identities>
			backend_record_identities{};
		std::atomic_uint32_t scene_owner_scoped_events{};
		std::atomic_uint32_t scene_owner_scoped_frame_events{};
		std::atomic_uint32_t scene_owner_unscoped_frame_events{};
		std::atomic_uint32_t scene_owner_thread_mismatches{};
		std::atomic_uint32_t distinct_scene_owner_records{};
		std::array<std::atomic_uintptr_t, maximum_unique_identities>
			scene_owner_record_identities{};
		std::atomic_uint32_t call_stack_samples{};
		std::atomic_uint32_t call_stack_capture_failures{};
		std::atomic_uint32_t call_stack_key_overflows{};
		std::atomic_uint32_t admission_collisions{};
		std::atomic_uint32_t classifier_thread_mismatches{};
		std::atomic_uint32_t active_writers{};
		std::atomic_uint64_t admission_gate{};
		std::atomic_uint32_t maximum_active_writers{};
		std::atomic_uint32_t distinct_contexts{};
		std::atomic_uint32_t distinct_threads{};
		std::array<std::atomic_uintptr_t, maximum_unique_identities> context_identities{};
		std::array<std::atomic_uint32_t, maximum_unique_identities> thread_identities{};
		std::atomic_bool identity_truncated{};
		std::atomic_uint32_t opaque_execute_command_lists{};
		std::atomic_uint32_t known_conversion_recordings{},known_conversion_executions{},known_conversion_replays{};
		thread_local ID3D11DeviceContext* executing_conversion{};
		std::array<std::atomic_uint32_t, api_count> per_api{};
		std::array<std::atomic_uint32_t, api_count> classifier_per_api{};
		std::array<std::atomic_uint32_t, api_count> frame_per_api{};
		std::array<event, maximum_events> captured_events{};
		report published_report{};
		std::atomic_bool report_ready{};

		std::atomic_uintptr_t classifier_record{};
		std::atomic_uint32_t classifier_record_type{};
		std::atomic_uint32_t classifier_thread_id{};
		std::atomic_uint64_t classifier_begin_qpc{};
		std::atomic_uint64_t classifier_end_qpc{};
		std::atomic_uint64_t frame_start_present_post{};
		std::atomic_uint64_t frame_end_present_pre{};
		std::atomic_uint64_t frame_end_present_post{};
		std::atomic_int32_t frame_present_result{};
		std::atomic_uint32_t frame_start_thread_id{};
		std::atomic_uint32_t frame_end_thread_id{};
		std::atomic_bool finalizer_claimed{};
		std::atomic_bool classifier_owner_active{};
		thread_local classifier_scope* active_classifier{};
		thread_local backend_record_context active_backend_record{};
		thread_local scene_owner_context active_scene_owner{};

		struct call_stack_key
		{
			std::uint64_t binding_sequence{};
			std::uint64_t execution_route{};
			std::uintptr_t caller{};
			api operation{api::draw_indexed};
		};

		thread_local std::array<call_stack_key, maximum_call_stack_keys>
			call_stack_keys{};
		thread_local std::uint32_t call_stack_key_count{};

#if defined(H2VR_EXECUTION_PROBE_TESTING)
		std::atomic_uint32_t admission_pause_stage{};
		std::atomic_bool admission_pause_entered{};
		std::atomic_bool admission_pause_released{};
#endif

		static_assert(std::is_trivially_copyable_v<event>);
		static_assert(std::is_trivially_copyable_v<report>);

		template <typename T, std::size_t Capacity>
		[[nodiscard]] bool note_unique(
			std::array<std::atomic<T>, Capacity>& values,
			std::atomic_uint32_t& count, const T value) noexcept
		{
			static_assert((Capacity & (Capacity - 1)) == 0);
			if (value == 0) return false;
			const auto start = static_cast<std::size_t>(
				static_cast<std::uint64_t>(value) * 11400714819323198485ull) &
				(Capacity - 1);
			for (std::size_t probe{}; probe < Capacity; ++probe)
			{
				auto& slot = values[(start + probe) & (Capacity - 1)];
				auto observed = slot.load(std::memory_order_acquire);
				if (observed == value) return true;
				if (observed == 0 && slot.compare_exchange_strong(observed, value,
					std::memory_order_acq_rel, std::memory_order_acquire))
				{
					count.fetch_add(1, std::memory_order_relaxed);
					return true;
				}
				// A competing observer may have installed this same identity while
				// our CAS was pending. compare_exchange updates `observed` on
				// failure, so recognise the shared identity instead of inserting a
				// duplicate into the next slot.
				if (observed == value) return true;
			}
			return false;
		}

		[[nodiscard]] std::uint64_t qpc() noexcept
		{
			LARGE_INTEGER value{};
			return QueryPerformanceCounter(&value)
				? static_cast<std::uint64_t>(value.QuadPart) : 0;
		}

		[[nodiscard]] constexpr std::size_t index_of(const api value) noexcept
		{
			return static_cast<std::size_t>(value);
		}

		void reset_call_stack_keys() noexcept
		{
			call_stack_key_count = 0;
			call_stack_keys = {};
		}

		[[nodiscard]] bool reserve_call_stack_sample(const event& value) noexcept
		{
			const auto execution_route = value.backend
				? (static_cast<std::uint64_t>(value.backend.record_type) << 32) |
					value.backend.target_id
				: (static_cast<std::uint64_t>(value.scene_owner.record_type) << 32) |
					value.scene_owner.target_selector;
			const call_stack_key key{
				value.output_binding.sequence,
				execution_route,
				value.caller,
				value.operation,
			};
			for (std::uint32_t index{}; index < call_stack_key_count; ++index)
			{
				const auto& existing = call_stack_keys[index];
				if (existing.binding_sequence == key.binding_sequence &&
					existing.execution_route == key.execution_route &&
					existing.caller == key.caller &&
					existing.operation == key.operation)
				{
					return false;
				}
			}
			if (call_stack_key_count >= call_stack_keys.size())
			{
				call_stack_key_overflows.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			call_stack_keys[call_stack_key_count++] = key;
			return true;
		}

		void capture_call_stack(event& value) noexcept
		{
			if (!reserve_call_stack_sample(value)) return;
			const auto depth = RtlCaptureStackBackTrace(0,
				static_cast<ULONG>(value.call_stack.size()),
				reinterpret_cast<PVOID*>(value.call_stack.data()), nullptr);
			value.call_stack_depth = static_cast<std::uint8_t>(depth);
			if (depth == 0)
			{
				call_stack_capture_failures.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			call_stack_samples.fetch_add(1, std::memory_order_relaxed);
		}

		void set_failure(const failure value) noexcept
		{
			failure expected = failure::none;
			(void)current_failure.compare_exchange_strong(expected, value,
				std::memory_order_acq_rel, std::memory_order_acquire);
			failure_requested.store(true, std::memory_order_release);
		}

		void update_writer_peak(const std::uint32_t value) noexcept
		{
			auto observed = maximum_active_writers.load(std::memory_order_relaxed);
			while (observed < value && !maximum_active_writers.compare_exchange_weak(
				observed, value, std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		[[nodiscard]] std::uint32_t admission_writer_count() noexcept
		{
			return static_cast<std::uint32_t>(admission_gate.load(
				std::memory_order_acquire) & admission_writer_mask);
		}

		[[nodiscard]] bool open_admission_gate(const std::uint8_t scope_flags) noexcept
		{
			const auto value = admission_open_bit |
				((scope_flags & frame_scope_flag) != 0 ? admission_frame_bit : 0);
			std::uint64_t expected{};
			return admission_gate.compare_exchange_strong(expected, value,
				std::memory_order_acq_rel, std::memory_order_acquire);
		}

		void close_admission_gate() noexcept
		{
			auto observed = admission_gate.load(std::memory_order_acquire);
			while ((observed & admission_open_bit) != 0)
			{
				const auto desired = (observed & admission_writer_mask) == 0
					? 0 : observed & ~admission_open_bit;
				if (admission_gate.compare_exchange_weak(observed, desired,
					std::memory_order_acq_rel, std::memory_order_acquire))
				{
					return;
				}
			}
		}

		[[nodiscard]] bool enter_admission_gate(std::uint8_t& scope_flags) noexcept
		{
			auto observed = admission_gate.load(std::memory_order_acquire);
			while ((observed & admission_open_bit) != 0)
			{
				if ((observed & admission_writer_mask) == admission_writer_mask)
				{
					return false;
				}
				if (admission_gate.compare_exchange_weak(observed, observed + 1,
					std::memory_order_acq_rel, std::memory_order_acquire))
				{
					scope_flags = (observed & admission_frame_bit) != 0
						? frame_scope_flag : classifier_scope_flag;
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool needs_observation(const api operation) noexcept
		{
			if (admission_gate.load(std::memory_order_acquire) & admission_open_bit) return true;
			const auto relevant = invocation_mask |
				(operation == api::execute_command_list ? opaque_mask : 0u) |
				(operation == api::draw_indexed ? indexed_mask : 0u);
			return (observer_mask.load(std::memory_order_acquire) & relevant) != 0;
		}

		[[nodiscard]] constexpr bool admission_state_matches_scope(
			const std::uint8_t scope_flags, const gate_state state) noexcept
		{
			return (scope_flags & frame_scope_flag) != 0
				? state == gate_state::frame_arming || state == gate_state::frame_active
				: state == gate_state::classifier_arming ||
					state == gate_state::classifier_active;
		}

#if defined(H2VR_EXECUTION_PROBE_TESTING)
		void pause_admission_for_test(const admission_test_stage stage) noexcept
		{
			auto expected = static_cast<std::uint32_t>(stage);
			if (!admission_pause_stage.compare_exchange_strong(expected, 0,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return;
			}
			admission_pause_entered.store(true, std::memory_order_release);
			while (!admission_pause_released.load(std::memory_order_acquire))
			{
				SwitchToThread();
			}
		}
#endif

		void assemble_report_locked() noexcept
		{
			if (report_ready.load(std::memory_order_acquire)) return;
			auto& output = published_report;
			std::memset(std::addressof(output), 0, sizeof(output));
			output.expected_context = observation_context.load(std::memory_order_acquire);
			output.device_generation = observation_generation.load(
				std::memory_order_acquire);
			output.classifier_record = classifier_record.load(std::memory_order_acquire);
			output.classifier_record_type = classifier_record_type.load(
				std::memory_order_acquire);
			output.classifier_thread_id = classifier_thread_id.load(
				std::memory_order_acquire);
			output.classifier_begin_qpc = classifier_begin_qpc.load(
				std::memory_order_acquire);
			output.classifier_end_qpc = classifier_end_qpc.load(
				std::memory_order_acquire);
			output.frame_start_present_post = frame_start_present_post.load(
				std::memory_order_acquire);
			output.frame_end_present_pre = frame_end_present_pre.load(
				std::memory_order_acquire);
			output.frame_end_present_post = frame_end_present_post.load(
				std::memory_order_acquire);
			output.frame_present_result = frame_present_result.load(
				std::memory_order_acquire);
			output.frame_start_thread_id = frame_start_thread_id.load(
				std::memory_order_acquire);
			output.frame_end_thread_id = frame_end_thread_id.load(
				std::memory_order_acquire);
			output.event_count = (std::min)(recorded_events.load(
				std::memory_order_acquire), static_cast<std::uint32_t>(output.events.size()));
			output.classifier_event_count = classifier_events.load(
				std::memory_order_acquire);
			output.frame_event_count = frame_events.load(std::memory_order_acquire);
			output.overflow_count = overflow_count.load(std::memory_order_acquire);
			output.foreign_context_events = foreign_context_events.load(
				std::memory_order_acquire);
			output.expected_context_frame_events = expected_context_frame_events.load(
				std::memory_order_acquire);
			output.backend_scoped_events = backend_scoped_events.load(
				std::memory_order_acquire);
			output.backend_scoped_frame_events = backend_scoped_frame_events.load(
				std::memory_order_acquire);
			output.backend_unscoped_frame_events = backend_unscoped_frame_events.load(
				std::memory_order_acquire);
			output.backend_thread_mismatches = backend_thread_mismatches.load(
				std::memory_order_acquire);
			output.distinct_backend_records = distinct_backend_records.load(
				std::memory_order_acquire);
			output.scene_owner_scoped_events = scene_owner_scoped_events.load(
				std::memory_order_acquire);
			output.scene_owner_scoped_frame_events =
				scene_owner_scoped_frame_events.load(std::memory_order_acquire);
			output.scene_owner_unscoped_frame_events =
				scene_owner_unscoped_frame_events.load(std::memory_order_acquire);
			output.scene_owner_thread_mismatches =
				scene_owner_thread_mismatches.load(std::memory_order_acquire);
			output.distinct_scene_owner_records =
				distinct_scene_owner_records.load(std::memory_order_acquire);
			output.call_stack_samples = call_stack_samples.load(
				std::memory_order_acquire);
			output.call_stack_capture_failures = call_stack_capture_failures.load(
				std::memory_order_acquire);
			output.call_stack_key_overflows = call_stack_key_overflows.load(
				std::memory_order_acquire);
			output.admission_collisions = admission_collisions.load(
				std::memory_order_acquire);
			output.classifier_thread_mismatches = classifier_thread_mismatches.load(
				std::memory_order_acquire);
			output.distinct_contexts = distinct_contexts.load(std::memory_order_acquire);
			output.distinct_threads = distinct_threads.load(std::memory_order_acquire);
			output.opaque_execute_command_lists = opaque_execute_command_lists.load(
				std::memory_order_acquire);
			output.known_conversion_recordings=known_conversion_recordings.load(std::memory_order_acquire);
			output.known_conversion_executions=known_conversion_executions.load(std::memory_order_acquire);
			output.known_conversion_replays=known_conversion_replays.load(std::memory_order_acquire);
			output.deferred_execution_opaque = output.opaque_execute_command_lists != 0;
			output.identity_truncated = identity_truncated.load(std::memory_order_acquire);
			for (std::size_t index{}; index < api_count; ++index)
			{
				output.per_api[index] = per_api[index].load(std::memory_order_acquire);
				output.classifier_per_api[index] = classifier_per_api[index].load(
					std::memory_order_acquire);
				output.frame_per_api[index] = frame_per_api[index].load(
					std::memory_order_acquire);
			}

			for (std::uint32_t index{}; index < output.event_count; ++index)
			{
				output.events[index] = captured_events[index];
			}
			report_ready.store(true, std::memory_order_release);
		}

		void finalize_report() noexcept
		{
			// Present-pre only closes the fixed event buffer and publishes terminal
			// scalars. The multi-megabyte immutable report is assembled lazily by
			// read_report() outside the Present/render hooks.
			if (active_writers.load(std::memory_order_acquire) != 0 ||
				admission_writer_count() != 0 ||
				classifier_owner_active.load(std::memory_order_acquire))
			{
				return;
			}
			auto closing = gate_state::frame_closing;
			if (!current_state.compare_exchange_strong(closing, gate_state::finalizing,
				std::memory_order_acq_rel, std::memory_order_acquire) ||
				finalizer_claimed.exchange(true, std::memory_order_acq_rel))
			{
				return;
			}

			const auto failed = failure_requested.load(std::memory_order_acquire) ||
				overflow_count.load(std::memory_order_acquire) != 0 ||
				admission_collisions.load(std::memory_order_acquire) != 0 ||
				identity_truncated.load(std::memory_order_acquire) ||
				classifier_record.load(std::memory_order_acquire) == 0 ||
				classifier_begin_qpc.load(std::memory_order_acquire) == 0 ||
				classifier_end_qpc.load(std::memory_order_acquire) <
					classifier_begin_qpc.load(std::memory_order_acquire) ||
				frame_start_present_post.load(std::memory_order_acquire) == 0 ||
				frame_end_present_pre.load(std::memory_order_acquire) !=
					frame_start_present_post.load(std::memory_order_acquire) + 1 ||
				frame_end_present_post.load(std::memory_order_acquire) !=
					frame_end_present_pre.load(std::memory_order_acquire) ||
				frame_present_result.load(std::memory_order_acquire) < 0 ||
				frame_start_thread_id.load(std::memory_order_acquire) !=
					frame_end_thread_id.load(std::memory_order_acquire);
			if (failed)
			{
				if (current_failure.load(std::memory_order_acquire) == failure::none)
				{
					set_failure(admission_collisions.load(std::memory_order_acquire) != 0
						? failure::admission_collision
						: (overflow_count.load(std::memory_order_acquire) != 0
						? failure::overflow
						: (identity_truncated.load(std::memory_order_acquire)
							? failure::identity_truncation : failure::frame_boundary)));
				}
				failures.fetch_add(1, std::memory_order_relaxed);
				current_state.store(gate_state::failed, std::memory_order_release);
			}
			else
			{
				completions.fetch_add(1, std::memory_order_relaxed);
				current_state.store(gate_state::complete, std::memory_order_release);
			}
		}

		void finish_classifier_close() noexcept
		{
			if (current_state.load(std::memory_order_acquire) !=
					gate_state::classifier_closing ||
				active_writers.load(std::memory_order_acquire) != 0 ||
				admission_writer_count() != 0)
			{
				return;
			}
			auto expected = gate_state::classifier_closing;
			(void)current_state.compare_exchange_strong(expected,
				gate_state::frame_pending, std::memory_order_release,
				std::memory_order_acquire);
		}

		void leave_writer() noexcept
		{
			(void)active_writers.fetch_sub(1, std::memory_order_acq_rel);
			const auto previous_gate = admission_gate.fetch_sub(1,
				std::memory_order_acq_rel);
			if ((previous_gate & admission_writer_mask) != 1) return;
			if ((previous_gate & admission_open_bit) == 0)
			{
				// The gate is closed and this was its final admitted writer. No
				// entrant can race this cleanup, so remove the retained scope tag.
				admission_gate.store(0, std::memory_order_release);
			}
			finish_classifier_close();
			finalize_report();
		}

		[[nodiscard]] constexpr bool is_metadata_arming_state(
			const gate_state state) noexcept
		{
			return state == gate_state::classifier_arming ||
				state == gate_state::classifier_end_arming ||
				state == gate_state::frame_arming ||
				state == gate_state::frame_end_arming ||
				state == gate_state::end_present_arming;
		}

		[[nodiscard]] bool request_terminal_failure(const failure value) noexcept
		{
			auto state = current_state.load(std::memory_order_acquire);
			for (;;)
			{
				if (state == gate_state::complete || state == gate_state::failed ||
					state == gate_state::finalizing ||
					state == gate_state::failure_closing ||
					state == gate_state::resetting)
				{
					return false;
				}
				// An arming owner has exclusive rights to its boundary fields. Mark
				// the failure, then either let that owner close after publishing or
				// retry if it already advanced the state.
				if (is_metadata_arming_state(state))
				{
					set_failure(value);
					const auto after = current_state.load(std::memory_order_acquire);
					if (after == state) return true;
					state = after;
					continue;
				}
				if (current_state.compare_exchange_weak(state,
					gate_state::failure_closing, std::memory_order_acq_rel,
					std::memory_order_acquire))
				{
					close_admission_gate();
					set_failure(value);
					current_state.store(gate_state::frame_closing,
						std::memory_order_release);
					finalize_report();
					return true;
				}
			}
		}

		void begin_invocation(invocation_scope& token, const api operation,
			ID3D11DeviceContext* const context,
			const std::uintptr_t caller, const std::array<std::uint64_t, 6>& arguments,
			const std::uint8_t argument_count,
			const engine_stereo_output_merger::binding_snapshot& output_binding) noexcept
		{
			if (operation == api::execute_command_list)
			{
				if (const auto observer = opaque_state_observer.load(
					std::memory_order_acquire))
				{
					observer(context,
						reinterpret_cast<ID3D11CommandList*>(arguments[0]),
						arguments[1] != 0, caller);
				}
			}
			for (auto& registered : invocation_observers)
			{
				if (const auto observer = registered.load(std::memory_order_acquire))
				{
					// ExecuteCommandList is deliberately reported too: consumers that
					// cannot decode deferred state may mark their provenance opaque while
					// ordinary draw/dispatch observers continue to ignore this enum value.
					observer(context, operation, caller, output_binding.target_id,
						output_binding.render_target_0, output_binding.sequence,
						argument_count, arguments);
				}
			}
			if (token.active) return;
			std::uint8_t scope_flags{};
			if (!enter_admission_gate(scope_flags)) return;

			// The packed gate claim is the admission linearization point. A closer
			// atomically removes its open bit and cannot publish terminal evidence
			// until every pre-close claim has either committed or hard-failed.
			const auto invocation_qpc = qpc();
			const auto writers = active_writers.fetch_add(1,
				std::memory_order_acq_rel) + 1;
#if defined(H2VR_EXECUTION_PROBE_TESTING)
			pause_admission_for_test(admission_test_stage::after_gate_claim);
#endif

			if (!admission_state_matches_scope(scope_flags,
				current_state.load(std::memory_order_acquire)))
			{
				admission_collisions.fetch_add(1, std::memory_order_relaxed);
				(void)request_terminal_failure(failure::admission_collision);
				leave_writer();
				return;
			}

#if defined(H2VR_EXECUTION_PROBE_TESTING)
			pause_admission_for_test(admission_test_stage::after_state_check);
#endif

			if (!admission_state_matches_scope(scope_flags,
				current_state.load(std::memory_order_acquire)))
			{
				admission_collisions.fetch_add(1, std::memory_order_relaxed);
				(void)request_terminal_failure(failure::admission_collision);
				leave_writer();
				return;
			}
			update_writer_peak(writers);
			// Native bootstrap must not classify the mod's private menu-canvas
			// recording as a foreign H2 context, or its already-tagged conversion
			// list as opaque. Observers above still see every call. Only this
			// bounded native census excludes these proven, state-restoring helpers;
			// unmarked lists, RestoreState=FALSE and foreign contexts remain strict.
			const auto native_context=reinterpret_cast<std::uintptr_t>(context)==expected_context.load(std::memory_order_acquire);
			const bool known_execution=native_context && operation==api::execute_command_list && arguments[1]!=0 &&
				native_conversion_command_list::is_marked(reinterpret_cast<ID3D11CommandList*>(arguments[0]));
			const bool known_recording=!native_context && operation!=api::execute_command_list &&
				native_conversion_command_list::is_recording_context(context);
			const bool known_replay=native_context && context==executing_conversion && operation!=api::execute_command_list;
			if(known_execution || known_recording || known_replay)
			{
				(known_execution?known_conversion_executions:known_recording?known_conversion_recordings:known_conversion_replays).fetch_add(1,std::memory_order_relaxed);
				token.active=true;token.known_conversion=true;return;
			}
			token.active = true;

			const auto api_index = index_of(operation);
			per_api[api_index].fetch_add(1, std::memory_order_relaxed);
			if ((scope_flags & classifier_scope_flag) != 0)
			{
				classifier_events.fetch_add(1, std::memory_order_relaxed);
				classifier_per_api[api_index].fetch_add(1, std::memory_order_relaxed);
			}
			if ((scope_flags & frame_scope_flag) != 0)
			{
				frame_events.fetch_add(1, std::memory_order_relaxed);
				frame_per_api[api_index].fetch_add(1, std::memory_order_relaxed);
			}
			if (operation == api::execute_command_list)
			{
				opaque_execute_command_lists.fetch_add(1, std::memory_order_relaxed);
			}
			const auto context_address = reinterpret_cast<std::uintptr_t>(context);
			const auto thread_id = static_cast<std::uint32_t>(GetCurrentThreadId());
			if (!note_unique(context_identities, distinct_contexts, context_address) ||
				!note_unique<std::uint32_t>(thread_identities, distinct_threads,
					thread_id))
			{
				identity_truncated.store(true, std::memory_order_release);
				set_failure(failure::identity_truncation);
			}

			const auto index = event_reservations.fetch_add(1,
				std::memory_order_relaxed);
			token.event_index = index;
			if (index >= captured_events.size())
			{
				overflow_count.fetch_add(1, std::memory_order_relaxed);
				set_failure(failure::overflow);
				return;
			}
			token.event_recorded = true;

			auto& output = captured_events[index];
			output = {};
			output.sequence = static_cast<std::uint64_t>(index) + 1;
			output.timestamp_qpc = invocation_qpc;
			output.context = context_address;
			output.caller = caller;
			output.thread_id = thread_id;
			output.operation = operation;
			output.scope_flags = scope_flags;
			output.argument_count = argument_count;
			output.arguments = arguments;
			output.expected_context = output.context != 0 && output.context ==
				expected_context.load(std::memory_order_acquire);
			if (!output.expected_context)
			{
				foreign_context_events.fetch_add(1, std::memory_order_relaxed);
			}
			else if ((scope_flags & frame_scope_flag) != 0)
			{
				expected_context_frame_events.fetch_add(1,
					std::memory_order_relaxed);
			}
			if ((scope_flags & classifier_scope_flag) != 0 &&
				output.thread_id != classifier_thread_id.load(std::memory_order_acquire))
			{
				classifier_thread_mismatches.fetch_add(1, std::memory_order_relaxed);
			}
			output.output_binding = output_binding;
			output.backend = active_backend_record;
			if (output.backend)
			{
				backend_scoped_events.fetch_add(1, std::memory_order_relaxed);
				output.backend_thread_match = output.backend.owner_thread_id != 0 &&
					output.backend.owner_thread_id == output.thread_id;
				if (!output.backend_thread_match)
				{
					backend_thread_mismatches.fetch_add(1, std::memory_order_relaxed);
				}
				if (!note_unique(backend_record_identities, distinct_backend_records,
					output.backend.record))
				{
					identity_truncated.store(true, std::memory_order_release);
					set_failure(failure::identity_truncation);
				}
				if ((scope_flags & frame_scope_flag) != 0)
				{
					backend_scoped_frame_events.fetch_add(1,
						std::memory_order_relaxed);
				}
			}
			else if ((scope_flags & frame_scope_flag) != 0)
			{
				backend_unscoped_frame_events.fetch_add(1,
					std::memory_order_relaxed);
			}
			output.scene_owner = active_scene_owner;
			if (output.scene_owner)
			{
				scene_owner_scoped_events.fetch_add(1, std::memory_order_relaxed);
				output.scene_owner_thread_match =
					output.scene_owner.owner_thread_id != 0 &&
					output.scene_owner.owner_thread_id == output.thread_id;
				if (!output.scene_owner_thread_match)
				{
					scene_owner_thread_mismatches.fetch_add(1,
						std::memory_order_relaxed);
				}
				if (!note_unique(scene_owner_record_identities,
					distinct_scene_owner_records, output.scene_owner.record))
				{
					identity_truncated.store(true, std::memory_order_release);
					set_failure(failure::identity_truncation);
				}
				if ((scope_flags & frame_scope_flag) != 0)
				{
					scene_owner_scoped_frame_events.fetch_add(1,
						std::memory_order_relaxed);
				}
			}
			else if ((scope_flags & frame_scope_flag) != 0)
			{
				scene_owner_unscoped_frame_events.fetch_add(1,
					std::memory_order_relaxed);
			}
			capture_call_stack(output);
		}

		void end_invocation(invocation_scope& token) noexcept
		{
			if (!token.active) return;
			if (token.event_recorded)
			{
				recorded_events.fetch_add(1, std::memory_order_release);
			}
			token = {};
			leave_writer();
		}

		void __stdcall draw_stub(ID3D11DeviceContext* const context,
			const UINT vertex_count, const UINT start_vertex_location)
		{
			const auto original = reinterpret_cast<draw_fn>(draw_hook.get_original());
			if (original == nullptr) return;
			if (!needs_observation(api::draw))
			{
				original(context, vertex_count, start_vertex_location);
				return;
			}
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			invocation_scope invocation{};
			begin_invocation(invocation, api::draw, context, caller,
				{vertex_count, start_vertex_location, 0, 0, 0, 0}, 2, binding);
			original(context, vertex_count, start_vertex_location);
			end_invocation(invocation);
		}

		void __stdcall draw_indexed_instanced_stub(ID3D11DeviceContext* const context,
			const UINT index_count, const UINT instance_count, const UINT start_index,
			const INT base_vertex, const UINT start_instance)
		{
			const auto original = reinterpret_cast<draw_indexed_instanced_fn>(
				draw_indexed_instanced_hook.get_original());
			if (original == nullptr) return;
			if (!needs_observation(api::draw_indexed_instanced))
			{
				original(context, index_count, instance_count, start_index, base_vertex, start_instance);
				return;
			}
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			invocation_scope invocation{};
			begin_invocation(invocation, api::draw_indexed_instanced, context, caller,
				{index_count, instance_count, start_index,
					static_cast<std::uint64_t>(static_cast<std::int64_t>(base_vertex)),
					start_instance, 0}, 5, binding);
			original(context, index_count, instance_count, start_index, base_vertex,
				start_instance);
			end_invocation(invocation);
		}

		void __stdcall draw_instanced_stub(ID3D11DeviceContext* const context,
			const UINT vertex_count, const UINT instance_count, const UINT start_vertex,
			const UINT start_instance)
		{
			const auto original = reinterpret_cast<draw_instanced_fn>(
				draw_instanced_hook.get_original());
			if (original == nullptr) return;
			if (!needs_observation(api::draw_instanced))
			{
				original(context, vertex_count, instance_count, start_vertex, start_instance);
				return;
			}
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			invocation_scope invocation{};
			begin_invocation(invocation, api::draw_instanced, context, caller,
				{vertex_count, instance_count, start_vertex, start_instance, 0, 0}, 4,
				binding);
			original(context, vertex_count, instance_count, start_vertex, start_instance);
			end_invocation(invocation);
		}

		void __stdcall draw_auto_stub(ID3D11DeviceContext* const context)
		{
			const auto original = reinterpret_cast<draw_auto_fn>(draw_auto_hook.get_original());
			if (original == nullptr) return;
			if (!needs_observation(api::draw_auto))
			{
				original(context);
				return;
			}
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			invocation_scope invocation{};
			begin_invocation(invocation, api::draw_auto, context, caller, {}, 0, binding);
			original(context);
			end_invocation(invocation);
		}

		void __stdcall draw_indexed_instanced_indirect_stub(
			ID3D11DeviceContext* const context, ID3D11Buffer* const buffer,
			const UINT offset)
		{
			const auto original = reinterpret_cast<indirect_fn>(
				draw_indexed_instanced_indirect_hook.get_original());
			if (original == nullptr) return;
			if (!needs_observation(api::draw_indexed_instanced_indirect))
			{
				original(context, buffer, offset);
				return;
			}
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			invocation_scope invocation{};
			begin_invocation(invocation, api::draw_indexed_instanced_indirect, context,
				caller,
				{reinterpret_cast<std::uintptr_t>(buffer), offset, 0, 0, 0, 0}, 2,
				binding);
			original(context, buffer, offset);
			end_invocation(invocation);
		}

		void __stdcall draw_instanced_indirect_stub(ID3D11DeviceContext* const context,
			ID3D11Buffer* const buffer, const UINT offset)
		{
			const auto original = reinterpret_cast<indirect_fn>(
				draw_instanced_indirect_hook.get_original());
			if (original == nullptr) return;
			if (!needs_observation(api::draw_instanced_indirect))
			{
				original(context, buffer, offset);
				return;
			}
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			invocation_scope invocation{};
			begin_invocation(invocation, api::draw_instanced_indirect, context, caller,
				{reinterpret_cast<std::uintptr_t>(buffer), offset, 0, 0, 0, 0}, 2,
				binding);
			original(context, buffer, offset);
			end_invocation(invocation);
		}

		void __stdcall dispatch_stub(ID3D11DeviceContext* const context,
			const UINT x, const UINT y, const UINT z)
		{
			const auto original = reinterpret_cast<dispatch_fn>(dispatch_hook.get_original());
			if (original == nullptr) return;
			if (!needs_observation(api::dispatch))
			{
				original(context, x, y, z);
				return;
			}
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			invocation_scope invocation{};
			begin_invocation(invocation, api::dispatch, context, caller,
				{x, y, z, 0, 0, 0}, 3, binding);
			original(context, x, y, z);
			end_invocation(invocation);
		}

		void __stdcall dispatch_indirect_stub(ID3D11DeviceContext* const context,
			ID3D11Buffer* const buffer, const UINT offset)
		{
			const auto original = reinterpret_cast<indirect_fn>(
				dispatch_indirect_hook.get_original());
			if (original == nullptr) return;
			if (!needs_observation(api::dispatch_indirect))
			{
				original(context, buffer, offset);
				return;
			}
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			invocation_scope invocation{};
			begin_invocation(invocation, api::dispatch_indirect, context, caller,
				{reinterpret_cast<std::uintptr_t>(buffer), offset, 0, 0, 0, 0}, 2,
				binding);
			original(context, buffer, offset);
			end_invocation(invocation);
		}

		void __stdcall execute_command_list_stub(ID3D11DeviceContext* const context,
			ID3D11CommandList* const commands, const BOOL restore_state)
		{
			const auto original = reinterpret_cast<execute_command_list_fn>(
				execute_command_list_hook.get_original());
			if (original == nullptr) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			invocation_scope invocation{};
			begin_invocation(invocation, api::execute_command_list, context, caller,
				{reinterpret_cast<std::uintptr_t>(commands),
					static_cast<std::uint64_t>(restore_state != FALSE), 0, 0, 0, 0}, 2,
				binding);
			// WARP (and some driver wrappers) re-enters Draw/Dispatch while
			// executing a list. Scope only the admitted, tagged list on this exact
			// thread/context; nested unknown command lists are still classified.
			const auto previous_conversion=executing_conversion;
			executing_conversion=invocation.known_conversion?context:nullptr;
			original(context, commands, restore_state);
			executing_conversion=previous_conversion;
			if (restore_state == FALSE)
			{
				engine_stereo_output_merger::invalidate_current_binding(context);
			}
			end_invocation(invocation);
		}

		struct hook_spec
		{
			api operation{};
			std::size_t slot{};
			utils::hook::detour* hook{};
			void* stub{};
		};

		[[nodiscard]] std::array<hook_spec, observer_hook_count> hook_specs() noexcept
		{
			return {{
				{api::draw, draw_slot, &draw_hook, reinterpret_cast<void*>(draw_stub)},
				{api::draw_indexed_instanced, draw_indexed_instanced_slot,
					&draw_indexed_instanced_hook,
					reinterpret_cast<void*>(draw_indexed_instanced_stub)},
				{api::draw_instanced, draw_instanced_slot, &draw_instanced_hook,
					reinterpret_cast<void*>(draw_instanced_stub)},
				{api::draw_auto, draw_auto_slot, &draw_auto_hook,
					reinterpret_cast<void*>(draw_auto_stub)},
				{api::draw_indexed_instanced_indirect,
					draw_indexed_instanced_indirect_slot,
					&draw_indexed_instanced_indirect_hook,
					reinterpret_cast<void*>(draw_indexed_instanced_indirect_stub)},
				{api::draw_instanced_indirect, draw_instanced_indirect_slot,
					&draw_instanced_indirect_hook,
					reinterpret_cast<void*>(draw_instanced_indirect_stub)},
				{api::dispatch, dispatch_slot, &dispatch_hook,
					reinterpret_cast<void*>(dispatch_stub)},
				{api::dispatch_indirect, dispatch_indirect_slot, &dispatch_indirect_hook,
					reinterpret_cast<void*>(dispatch_indirect_stub)},
				{api::execute_command_list, execute_command_list_slot,
					&execute_command_list_hook,
					reinterpret_cast<void*>(execute_command_list_stub)},
			}};
		}
	}

	void set_invocation_observer(const invocation_observer_channel channel,
		const invocation_observer_fn observer) noexcept
	{
		const auto slot = static_cast<std::size_t>(channel);
		if (slot >= invocation_observers.size()) return;
		const std::lock_guard lock(observer_registration_mutex);
		invocation_observers[slot].store(observer, std::memory_order_release);
		if (observer) observer_mask.fetch_or(1u << slot, std::memory_order_release);
		else observer_mask.fetch_and(~(1u << slot), std::memory_order_release);
	}

	void set_opaque_state_observer(const opaque_state_observer_fn observer) noexcept
	{
		const std::lock_guard lock(observer_registration_mutex);
		opaque_state_observer.store(observer, std::memory_order_release);
		if (observer) observer_mask.fetch_or(opaque_mask, std::memory_order_release);
		else observer_mask.fetch_and(~opaque_mask, std::memory_order_release);
	}

	void set_draw_indexed_observer(const draw_indexed_observer_fn observer) noexcept
	{
		const std::lock_guard lock(observer_registration_mutex);
		draw_indexed_observer.store(observer, std::memory_order_release);
		if (observer) observer_mask.fetch_or(indexed_mask, std::memory_order_release);
		else observer_mask.fetch_and(~indexed_mask, std::memory_order_release);
	}

	bool install(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		const auto install_state = current_state.load(std::memory_order_acquire);
		if (install_state == gate_state::complete ||
			install_state == gate_state::failed)
		{
			// Terminal evidence is immutable. A later device callback may only
			// verify that the process-wide implementation targets are unchanged;
			// it must not retarget this completed observation to a new device.
			if (context == nullptr || device_generation == 0) return false;
			auto* const vtable = *reinterpret_cast<void***>(context);
			if (vtable == nullptr ||
				hook_targets[index_of(api::draw_indexed)].load(
					std::memory_order_acquire) != reinterpret_cast<std::uintptr_t>(
						vtable[draw_indexed_slot]) ||
				output_merger_target.load(std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(vtable[output_merger_slot]) ||
				output_merger_unordered_access_target.load(std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(
						vtable[output_merger_unordered_access_slot]) ||
				clear_state_target.load(std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(vtable[clear_state_slot]))
			{
				return false;
			}
			for (const auto& spec : hook_specs())
			{
				if (hook_targets[index_of(spec.operation)].load(
						std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(vtable[spec.slot]))
				{
					return false;
				}
			}
			return hooks_installed.load(std::memory_order_acquire) &&
				draw_indexed_forwarding_verified.load(std::memory_order_acquire) &&
				targets_distinct.load(std::memory_order_acquire) &&
				!installation_permanently_failed.load(std::memory_order_acquire);
		}
		if (installation_permanently_failed.load(std::memory_order_acquire) ||
			context == nullptr || device_generation == 0)
		{
			hook_failures.fetch_add(1, std::memory_order_relaxed);
			installation_permanently_failed.store(true, std::memory_order_release);
			(void)request_terminal_failure(failure::install);
			return false;
		}
		try
		{
			auto* const vtable = *reinterpret_cast<void***>(context);
			if (vtable == nullptr) throw std::runtime_error("D3D11 context vtable is null");
			const auto draw_indexed_status = engine_stereo_draw_indexed::get_status();
			const auto output_merger_status =
				engine_stereo_output_merger::get_status();
			if (!draw_indexed_status.hook_installed ||
				draw_indexed_status.hook_failures != 0 ||
				draw_indexed_status.hook_target != reinterpret_cast<std::uintptr_t>(
					vtable[draw_indexed_slot]))
			{
				throw std::runtime_error("external D3D11 DrawIndexed observer is unavailable");
			}
			if (!output_merger_status.hook_installed ||
				!output_merger_status.extended_hooks_installed ||
				output_merger_status.hook_failures != 0)
			{
				throw std::runtime_error("external D3D11 output binding observers are unavailable");
			}

			const auto specs = hook_specs();
			std::array<std::uintptr_t, api_count + 3> all_targets{};
			std::size_t target_count{};
			const auto append_target = [&](void* const target)
			{
				if (!utils::hook_validation::validate_executable_target(target))
				{
					throw std::runtime_error("D3D11 census target is not executable");
				}
				all_targets[target_count++] = reinterpret_cast<std::uintptr_t>(target);
			};
			append_target(vtable[draw_indexed_slot]);
			for (const auto& spec : specs) append_target(vtable[spec.slot]);
			append_target(reinterpret_cast<void*>(output_merger_status.hook_target));
			append_target(reinterpret_cast<void*>(
				output_merger_status.unordered_access_hook_target));
			append_target(reinterpret_cast<void*>(
				output_merger_status.clear_state_hook_target));
			if (output_merger_status.hook_target != reinterpret_cast<std::uintptr_t>(
					vtable[output_merger_slot]) ||
				output_merger_status.unordered_access_hook_target !=
					reinterpret_cast<std::uintptr_t>(
						vtable[output_merger_unordered_access_slot]) ||
				output_merger_status.clear_state_hook_target !=
					reinterpret_cast<std::uintptr_t>(vtable[clear_state_slot]))
			{
				throw std::runtime_error("external D3D11 output binding target mismatch");
			}
			for (std::size_t left{}; left < target_count; ++left)
			{
				for (auto right = left + 1; right < target_count; ++right)
				{
					if (all_targets[left] == all_targets[right])
					{
						throw std::runtime_error("D3D11 census targets alias");
					}
				}
			}
			targets_distinct.store(true, std::memory_order_release);

			const std::lock_guard lock(hook_mutex);
			for (const auto& spec : specs)
			{
				const auto target = reinterpret_cast<std::uintptr_t>(vtable[spec.slot]);
				const auto existing = hook_targets[index_of(spec.operation)].load(
					std::memory_order_acquire);
				if (existing != 0 && existing != target)
				{
					throw std::runtime_error("D3D11 execution target changed across devices");
				}
			}

			installed_hook_count.store(0, std::memory_order_release);
			hook_targets[index_of(api::draw_indexed)].store(
				reinterpret_cast<std::uintptr_t>(vtable[draw_indexed_slot]),
				std::memory_order_release);
			output_merger_target.store(output_merger_status.hook_target,
				std::memory_order_release);
			output_merger_unordered_access_target.store(
				output_merger_status.unordered_access_hook_target,
				std::memory_order_release);
			clear_state_target.store(output_merger_status.clear_state_hook_target,
				std::memory_order_release);
			for (const auto& spec : specs)
			{
				void* const target = vtable[spec.slot];
				if (!spec.hook->is_enabled())
				{
					if (spec.hook->get_original() == nullptr)
					{
						spec.hook->create(target, spec.stub);
					}
					else
					{
						spec.hook->enable();
					}
				}
				hook_targets[index_of(spec.operation)].store(
					reinterpret_cast<std::uintptr_t>(target), std::memory_order_release);
				installed_hook_count.fetch_add(1, std::memory_order_release);
			}
			expected_context.store(reinterpret_cast<std::uintptr_t>(context),
				std::memory_order_release);
			expected_generation.store(device_generation, std::memory_order_release);
			hooks_installed.store(true, std::memory_order_release);
			draw_indexed_forwarding_verified.store(true, std::memory_order_release);
			return true;
		}
		catch (...)
		{
			hook_failures.fetch_add(1, std::memory_order_relaxed);
			// Never unhook a partially installed process-wide observer set. Every
			// enabled stub remains a permanent pass-through because the census is
			// terminally failed and cannot be re-armed in this process.
			installation_permanently_failed.store(true, std::memory_order_release);
			hooks_installed.store(false, std::memory_order_release);
			(void)request_terminal_failure(failure::install);
			return false;
		}
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		if (expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context) ||
			expected_generation.load(std::memory_order_acquire) != device_generation)
		{
			return;
		}
		if (!request_terminal_failure(failure::device_invalidated)) return;
		expected_context.store(0, std::memory_order_release);
		expected_generation.store(0, std::memory_order_release);
	}

	bool begin_classifier(classifier_scope& output, const std::uintptr_t record,
		const std::uint32_t record_type) noexcept
	{
		const auto state = current_state.load(std::memory_order_acquire);
		if (state == gate_state::classifier_arming ||
			state == gate_state::classifier_active ||
			state == gate_state::classifier_end_arming ||
			state == gate_state::classifier_closing)
		{
			(void)request_terminal_failure(failure::classifier_collision);
			return false;
		}
		if (output.active || active_classifier != nullptr ||
			classifier_owner_active.load(std::memory_order_acquire) || record == 0 ||
			!hooks_installed.load(std::memory_order_acquire) ||
			expected_context.load(std::memory_order_acquire) == 0 ||
			expected_generation.load(std::memory_order_acquire) == 0)
		{
			return false;
		}
		auto expected = gate_state::classifier_pending;
		if (!current_state.compare_exchange_strong(expected,
			gate_state::classifier_arming, std::memory_order_acq_rel,
			std::memory_order_acquire))
		{
			if (expected == gate_state::classifier_arming ||
				expected == gate_state::classifier_active ||
				expected == gate_state::classifier_end_arming ||
				expected == gate_state::classifier_closing)
			{
				(void)request_terminal_failure(failure::classifier_collision);
			}
			return false;
		}
		if (classifier_owner_active.exchange(true, std::memory_order_acq_rel))
		{
			(void)request_terminal_failure(failure::classifier_contract);
			return false;
		}
		reset_call_stack_keys();

		output.active = true;
		output.record = record;
		output.record_type = record_type;
		output.thread_id = GetCurrentThreadId();
		output.begin_qpc = qpc();
		classifier_record.store(record, std::memory_order_release);
		classifier_record_type.store(record_type, std::memory_order_release);
		classifier_thread_id.store(output.thread_id, std::memory_order_release);
		classifier_begin_qpc.store(output.begin_qpc, std::memory_order_release);
		observation_context.store(expected_context.load(std::memory_order_acquire),
			std::memory_order_release);
		observation_generation.store(expected_generation.load(std::memory_order_acquire),
			std::memory_order_release);
		active_classifier = &output;
		auto arming = gate_state::classifier_arming;
		if (failure_requested.load(std::memory_order_acquire))
		{
			active_classifier = nullptr;
			output.active = false;
			classifier_owner_active.store(false, std::memory_order_release);
			current_state.store(gate_state::frame_closing,
				std::memory_order_release);
			finalize_report();
			return false;
		}
		if (!open_admission_gate(classifier_scope_flag))
		{
			active_classifier = nullptr;
			output.active = false;
			classifier_owner_active.store(false, std::memory_order_release);
			set_failure(failure::classifier_contract);
			current_state.store(gate_state::frame_closing,
				std::memory_order_release);
			finalize_report();
			return false;
		}
#if defined(H2VR_EXECUTION_PROBE_TESTING)
		pause_admission_for_test(admission_test_stage::after_gate_open);
#endif
		if (!current_state.compare_exchange_strong(arming,
			gate_state::classifier_active, std::memory_order_acq_rel,
			std::memory_order_acquire))
		{
			close_admission_gate();
			active_classifier = nullptr;
			output.active = false;
			classifier_owner_active.store(false, std::memory_order_release);
			finalize_report();
			return false;
		}
		if (failure_requested.load(std::memory_order_acquire))
		{
			close_admission_gate();
			active_classifier = nullptr;
			output.active = false;
			classifier_owner_active.store(false, std::memory_order_release);
			(void)request_terminal_failure(current_failure.load(
				std::memory_order_acquire));
			return false;
		}
		attempts.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	bool end_classifier(classifier_scope& active) noexcept
	{
		if (!active.active)
		{
			return false;
		}
		if (active_classifier != &active)
		{
			(void)request_terminal_failure(failure::classifier_contract);
			return false;
		}
		const auto contract_valid = GetCurrentThreadId() == active.thread_id &&
			active.record == classifier_record.load(std::memory_order_acquire) &&
			active.record_type == classifier_record_type.load(std::memory_order_acquire);
		auto expected = gate_state::classifier_active;
		if (!current_state.compare_exchange_strong(expected,
			gate_state::classifier_end_arming, std::memory_order_acq_rel,
			std::memory_order_acquire))
		{
			active_classifier = nullptr;
			active.active = false;
			classifier_owner_active.store(false, std::memory_order_release);
			if (expected != gate_state::complete && expected != gate_state::failed &&
				expected != gate_state::finalizing)
			{
				(void)request_terminal_failure(failure::classifier_contract);
			}
			finalize_report();
			return false;
		}
		close_admission_gate();
		classifier_end_qpc.store(qpc(), std::memory_order_relaxed);
		active_classifier = nullptr;
		active.active = false;
		classifier_owner_active.store(false, std::memory_order_release);
		if (!contract_valid) set_failure(failure::classifier_contract);
		current_state.store(gate_state::classifier_closing, std::memory_order_release);
		if (failure_requested.load(std::memory_order_acquire))
		{
			(void)request_terminal_failure(current_failure.load(
				std::memory_order_acquire));
			return false;
		}
		finish_classifier_close();
		return true;
	}

	void set_backend_record_context(const backend_record_context& context) noexcept
	{
		active_backend_record = context;
	}

	void clear_backend_record_context() noexcept
	{
		active_backend_record = {};
	}

	void set_scene_owner_context(const scene_owner_context& context) noexcept
	{
		active_scene_owner = context;
	}

	void clear_scene_owner_context() noexcept
	{
		active_scene_owner = {};
	}

	void begin_draw_indexed(invocation_scope& output,
		ID3D11DeviceContext* const context,
		const UINT index_count, const UINT start_index_location,
		const INT base_vertex_location, const std::uintptr_t caller) noexcept
	{
		if (!needs_observation(api::draw_indexed)) return;
		const auto binding = engine_stereo_output_merger::get_current_binding(context);
		begin_invocation(output, api::draw_indexed, context, caller,
			{index_count, start_index_location,
				static_cast<std::uint64_t>(static_cast<std::int64_t>(base_vertex_location)),
				0, 0, 0}, 3, binding);
		if (const auto observer = draw_indexed_observer.load(
			std::memory_order_acquire))
		{
			observer(context, caller, binding.target_id);
		}
	}

	void end_draw_indexed(invocation_scope& active) noexcept
	{
		end_invocation(active);
	}

	void on_present_pre(const std::uint64_t frame_index,
		const std::uint64_t device_generation, const std::uint32_t thread_id) noexcept
	{
		auto expected = gate_state::frame_active;
		if (!current_state.compare_exchange_strong(expected,
			gate_state::frame_end_arming, std::memory_order_acq_rel,
			std::memory_order_acquire))
		{
			return;
		}
		close_admission_gate();
		frame_end_present_pre.store(frame_index, std::memory_order_relaxed);
		frame_end_thread_id.store(thread_id, std::memory_order_relaxed);
		if (device_generation != observation_generation.load(std::memory_order_acquire))
		{
			set_failure(failure::device_generation);
		}
		if (frame_index != frame_start_present_post.load(std::memory_order_acquire) + 1)
		{
			set_failure(failure::frame_boundary);
		}
		if (thread_id != frame_start_thread_id.load(std::memory_order_acquire))
		{
			set_failure(failure::present_owner_mismatch);
		}
		current_state.store(gate_state::awaiting_end_present_post,
			std::memory_order_release);
		if (failure_requested.load(std::memory_order_acquire))
		{
			(void)request_terminal_failure(current_failure.load(
				std::memory_order_acquire));
		}
	}

	void on_present_post(const std::uint64_t frame_index,
		const std::uint64_t device_generation, const std::uint32_t thread_id,
		const HRESULT result) noexcept
	{
		auto state = current_state.load(std::memory_order_acquire);
		if (state == gate_state::awaiting_end_present_post)
		{
			auto expected = gate_state::awaiting_end_present_post;
			if (!current_state.compare_exchange_strong(expected,
				gate_state::end_present_arming, std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				return;
			}
			frame_end_present_post.store(frame_index, std::memory_order_relaxed);
			frame_present_result.store(result, std::memory_order_relaxed);
			if (frame_index != frame_end_present_pre.load(std::memory_order_acquire))
			{
				set_failure(failure::frame_boundary);
			}
			if (thread_id != frame_start_thread_id.load(std::memory_order_acquire) ||
				thread_id != frame_end_thread_id.load(std::memory_order_acquire))
			{
				set_failure(failure::present_owner_mismatch);
			}
			if (device_generation != observation_generation.load(
				std::memory_order_acquire))
			{
				set_failure(failure::device_generation);
			}
			if (FAILED(result)) set_failure(failure::present_failed);
			current_state.store(gate_state::frame_closing, std::memory_order_release);
			finalize_report();
			return;
		}
		if (FAILED(result) || state != gate_state::frame_pending)
		{
			return;
		}
		auto expected = gate_state::frame_pending;
		if (!current_state.compare_exchange_strong(expected, gate_state::frame_arming,
			std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return;
		}
		frame_start_present_post.store(frame_index, std::memory_order_relaxed);
		frame_start_thread_id.store(thread_id, std::memory_order_relaxed);
		reset_call_stack_keys();
		if (device_generation != observation_generation.load(std::memory_order_acquire))
		{
			set_failure(failure::device_generation);
		}
		if (!open_admission_gate(frame_scope_flag))
		{
			set_failure(failure::frame_boundary);
			current_state.store(gate_state::frame_closing, std::memory_order_release);
			finalize_report();
			return;
		}
#if defined(H2VR_EXECUTION_PROBE_TESTING)
		pause_admission_for_test(admission_test_stage::after_gate_open);
#endif
		current_state.store(gate_state::frame_active, std::memory_order_release);
		if (failure_requested.load(std::memory_order_acquire))
		{
			(void)request_terminal_failure(current_failure.load(
				std::memory_order_acquire));
		}
	}

	bool bootstrap_complete() noexcept
	{
		return current_state.load(std::memory_order_acquire) == gate_state::complete;
	}

	status get_status() noexcept
	{
		status output{};
		output.state = current_state.load(std::memory_order_acquire);
		output.error = current_failure.load(std::memory_order_acquire);
		output.hooks_installed = hooks_installed.load(std::memory_order_acquire);
		output.draw_indexed_forwarding_external =
			draw_indexed_forwarding_verified.load(std::memory_order_acquire);
		output.report_ready = report_ready.load(std::memory_order_acquire);
		output.targets_distinct = targets_distinct.load(std::memory_order_acquire);
		output.installation_permanently_failed =
			installation_permanently_failed.load(std::memory_order_acquire);
		output.deferred_context_probe_attempted = deferred_context_probe_attempted.load(
			std::memory_order_acquire);
		output.deferred_context_probe_succeeded = deferred_context_probe_succeeded.load(
			std::memory_order_acquire);
		output.deferred_execution_opaque = opaque_execute_command_lists.load(
			std::memory_order_acquire) != 0;
		output.identity_truncated = identity_truncated.load(std::memory_order_acquire);
		output.installed_hook_count = installed_hook_count.load(std::memory_order_acquire);
		for (std::size_t index{}; index < api_count; ++index)
		{
			output.hook_targets[index] = hook_targets[index].load(std::memory_order_acquire);
			output.per_api[index] = per_api[index].load(std::memory_order_acquire);
			output.classifier_per_api[index] = classifier_per_api[index].load(
				std::memory_order_acquire);
			output.frame_per_api[index] = frame_per_api[index].load(
				std::memory_order_acquire);
			output.deferred_target_matches[index] = deferred_target_matches[index].load(
				std::memory_order_acquire);
		}
		output.output_merger_target = output_merger_target.load(
			std::memory_order_acquire);
		output.output_merger_unordered_access_target =
			output_merger_unordered_access_target.load(std::memory_order_acquire);
		output.clear_state_target = clear_state_target.load(std::memory_order_acquire);
		output.deferred_output_merger_target_matches =
			deferred_output_merger_target_matches.load(std::memory_order_acquire);
		output.expected_context = expected_context.load(std::memory_order_acquire);
		output.device_generation = expected_generation.load(std::memory_order_acquire);
		output.hook_failures = hook_failures.load(std::memory_order_acquire);
		output.attempts = attempts.load(std::memory_order_acquire);
		output.completions = completions.load(std::memory_order_acquire);
		output.failures = failures.load(std::memory_order_acquire);
		output.event_reservations = event_reservations.load(std::memory_order_acquire);
		output.recorded_events = recorded_events.load(std::memory_order_acquire);
		output.classifier_events = classifier_events.load(std::memory_order_acquire);
		output.frame_events = frame_events.load(std::memory_order_acquire);
		output.overflows = overflow_count.load(std::memory_order_acquire);
		output.foreign_context_events = foreign_context_events.load(
			std::memory_order_acquire);
		output.expected_context_frame_events = expected_context_frame_events.load(
			std::memory_order_acquire);
		output.backend_scoped_events = backend_scoped_events.load(
			std::memory_order_acquire);
		output.backend_scoped_frame_events = backend_scoped_frame_events.load(
			std::memory_order_acquire);
		output.backend_unscoped_frame_events = backend_unscoped_frame_events.load(
			std::memory_order_acquire);
		output.backend_thread_mismatches = backend_thread_mismatches.load(
			std::memory_order_acquire);
		output.distinct_backend_records = distinct_backend_records.load(
			std::memory_order_acquire);
		output.scene_owner_scoped_events = scene_owner_scoped_events.load(
			std::memory_order_acquire);
		output.scene_owner_scoped_frame_events =
			scene_owner_scoped_frame_events.load(std::memory_order_acquire);
		output.scene_owner_unscoped_frame_events =
			scene_owner_unscoped_frame_events.load(std::memory_order_acquire);
		output.scene_owner_thread_mismatches =
			scene_owner_thread_mismatches.load(std::memory_order_acquire);
		output.distinct_scene_owner_records =
			distinct_scene_owner_records.load(std::memory_order_acquire);
		output.call_stack_samples = call_stack_samples.load(
			std::memory_order_acquire);
		output.call_stack_capture_failures = call_stack_capture_failures.load(
			std::memory_order_acquire);
		output.call_stack_key_overflows = call_stack_key_overflows.load(
			std::memory_order_acquire);
		output.admission_collisions = admission_collisions.load(
			std::memory_order_acquire);
		output.classifier_thread_mismatches = classifier_thread_mismatches.load(
			std::memory_order_acquire);
		output.active_writers = active_writers.load(std::memory_order_acquire);
		output.maximum_active_writers = maximum_active_writers.load(
			std::memory_order_acquire);
		output.distinct_contexts = distinct_contexts.load(std::memory_order_acquire);
		output.distinct_threads = distinct_threads.load(std::memory_order_acquire);
		output.draw_indexed_forwarded_calls = per_api[index_of(api::draw_indexed)].load(
			std::memory_order_acquire);
		output.opaque_execute_command_lists = opaque_execute_command_lists.load(
			std::memory_order_acquire);
		output.known_conversion_recordings=known_conversion_recordings.load(std::memory_order_acquire);
		output.known_conversion_executions=known_conversion_executions.load(std::memory_order_acquire);
		output.known_conversion_replays=known_conversion_replays.load(std::memory_order_acquire);
		output.classifier_record = classifier_record.load(std::memory_order_acquire);
		output.classifier_record_type = classifier_record_type.load(
			std::memory_order_acquire);
		output.classifier_thread_id = classifier_thread_id.load(
			std::memory_order_acquire);
		output.classifier_begin_qpc = classifier_begin_qpc.load(
			std::memory_order_acquire);
		output.classifier_end_qpc = classifier_end_qpc.load(
			std::memory_order_acquire);
		output.frame_start_present_post = frame_start_present_post.load(
			std::memory_order_acquire);
		output.frame_end_present_pre = frame_end_present_pre.load(
			std::memory_order_acquire);
		output.frame_end_present_post = frame_end_present_post.load(
			std::memory_order_acquire);
		output.frame_present_result = frame_present_result.load(
			std::memory_order_acquire);
		output.frame_start_thread_id = frame_start_thread_id.load(
			std::memory_order_acquire);
		output.frame_end_thread_id = frame_end_thread_id.load(
			std::memory_order_acquire);
		return output;
	}

	bool read_report(report& output) noexcept
	{
		auto state = current_state.load(std::memory_order_acquire);
		if (state != gate_state::complete && state != gate_state::failed) return false;
		const std::lock_guard lock(report_mutex);
		state = current_state.load(std::memory_order_acquire);
		if (state != gate_state::complete && state != gate_state::failed) return false;
		assemble_report_locked();
		output = published_report;
		return true;
	}

	const char* to_string(const gate_state state) noexcept
	{
		switch (state)
		{
		case gate_state::classifier_pending: return "classifier_pending";
		case gate_state::classifier_arming: return "classifier_arming";
		case gate_state::classifier_active: return "classifier_active";
		case gate_state::classifier_end_arming: return "classifier_end_arming";
		case gate_state::classifier_closing: return "classifier_closing";
		case gate_state::frame_pending: return "frame_pending";
		case gate_state::frame_arming: return "frame_arming";
		case gate_state::frame_active: return "frame_active";
		case gate_state::frame_end_arming: return "frame_end_arming";
		case gate_state::awaiting_end_present_post: return "awaiting_end_present_post";
		case gate_state::end_present_arming: return "end_present_arming";
		case gate_state::frame_closing: return "frame_closing";
		case gate_state::failure_closing: return "failure_closing";
		case gate_state::finalizing: return "finalizing";
		case gate_state::resetting: return "resetting";
		case gate_state::complete: return "complete";
		case gate_state::failed: return "failed";
		default: return "unknown";
		}
	}

	const char* to_string(const api value) noexcept
	{
		switch (value)
		{
		case api::draw_indexed: return "DrawIndexed";
		case api::draw: return "Draw";
		case api::draw_indexed_instanced: return "DrawIndexedInstanced";
		case api::draw_instanced: return "DrawInstanced";
		case api::draw_auto: return "DrawAuto";
		case api::draw_indexed_instanced_indirect: return "DrawIndexedInstancedIndirect";
		case api::draw_instanced_indirect: return "DrawInstancedIndirect";
		case api::dispatch: return "Dispatch";
		case api::dispatch_indirect: return "DispatchIndirect";
		case api::execute_command_list: return "ExecuteCommandList";
		default: return "unknown";
		}
	}

	const char* to_string(const failure value) noexcept
	{
		switch (value)
		{
		case failure::none: return "none";
		case failure::install: return "install";
		case failure::device_invalidated: return "device_invalidated";
		case failure::classifier_contract: return "classifier_contract";
		case failure::classifier_collision: return "classifier_collision";
		case failure::admission_collision: return "admission_collision";
		case failure::frame_boundary: return "frame_boundary";
		case failure::present_failed: return "present_failed";
		case failure::present_owner_mismatch: return "present_owner_mismatch";
		case failure::device_generation: return "device_generation";
		case failure::overflow: return "overflow";
		case failure::identity_truncation: return "identity_truncation";
		default: return "unknown";
		}
	}

#if defined(H2VR_EXECUTION_PROBE_TESTING)
	void test_arm_admission_pause(const admission_test_stage stage) noexcept
	{
		admission_pause_entered.store(false, std::memory_order_relaxed);
		admission_pause_released.store(false, std::memory_order_relaxed);
		admission_pause_stage.store(static_cast<std::uint32_t>(stage),
			std::memory_order_release);
	}

	bool test_admission_pause_entered() noexcept
	{
		return admission_pause_entered.load(std::memory_order_acquire);
	}

	void test_release_admission_pause() noexcept
	{
		admission_pause_released.store(true, std::memory_order_release);
	}
#endif

	bool reset() noexcept
	{
		if (installation_permanently_failed.load(std::memory_order_acquire))
		{
			return false;
		}
		auto state = current_state.load(std::memory_order_acquire);
		const auto resettable = state == gate_state::classifier_pending ||
			state == gate_state::frame_pending || state == gate_state::complete ||
			state == gate_state::failed;
		if (!resettable || active_writers.load(std::memory_order_acquire) != 0 ||
			admission_gate.load(std::memory_order_acquire) != 0 ||
			classifier_owner_active.load(std::memory_order_acquire) ||
			active_classifier != nullptr)
		{
			return false;
		}
		if (!current_state.compare_exchange_strong(state, gate_state::resetting,
			std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return false;
		}
		const std::lock_guard report_lock(report_mutex);
		attempts.store(0, std::memory_order_relaxed);
		completions.store(0, std::memory_order_relaxed);
		failures.store(0, std::memory_order_relaxed);
		event_reservations.store(0, std::memory_order_relaxed);
		recorded_events.store(0, std::memory_order_relaxed);
		classifier_events.store(0, std::memory_order_relaxed);
		frame_events.store(0, std::memory_order_relaxed);
		overflow_count.store(0, std::memory_order_relaxed);
		foreign_context_events.store(0, std::memory_order_relaxed);
		expected_context_frame_events.store(0, std::memory_order_relaxed);
		backend_scoped_events.store(0, std::memory_order_relaxed);
		backend_scoped_frame_events.store(0, std::memory_order_relaxed);
		backend_unscoped_frame_events.store(0, std::memory_order_relaxed);
		backend_thread_mismatches.store(0, std::memory_order_relaxed);
		distinct_backend_records.store(0, std::memory_order_relaxed);
		scene_owner_scoped_events.store(0, std::memory_order_relaxed);
		scene_owner_scoped_frame_events.store(0, std::memory_order_relaxed);
		scene_owner_unscoped_frame_events.store(0, std::memory_order_relaxed);
		scene_owner_thread_mismatches.store(0, std::memory_order_relaxed);
		distinct_scene_owner_records.store(0, std::memory_order_relaxed);
		call_stack_samples.store(0, std::memory_order_relaxed);
		call_stack_capture_failures.store(0, std::memory_order_relaxed);
		call_stack_key_overflows.store(0, std::memory_order_relaxed);
		admission_collisions.store(0, std::memory_order_relaxed);
		classifier_thread_mismatches.store(0, std::memory_order_relaxed);
		maximum_active_writers.store(0, std::memory_order_relaxed);
		admission_gate.store(0, std::memory_order_relaxed);
		distinct_contexts.store(0, std::memory_order_relaxed);
		distinct_threads.store(0, std::memory_order_relaxed);
		identity_truncated.store(false, std::memory_order_relaxed);
		opaque_execute_command_lists.store(0, std::memory_order_relaxed);
		known_conversion_recordings.store(0,std::memory_order_relaxed);
		known_conversion_executions.store(0,std::memory_order_relaxed);
		known_conversion_replays.store(0,std::memory_order_relaxed);
		for (auto& identity : context_identities)
		{
			identity.store(0, std::memory_order_relaxed);
		}
		for (auto& identity : thread_identities)
		{
			identity.store(0, std::memory_order_relaxed);
		}
		for (auto& identity : backend_record_identities)
		{
			identity.store(0, std::memory_order_relaxed);
		}
		for (auto& identity : scene_owner_record_identities)
		{
			identity.store(0, std::memory_order_relaxed);
		}
		active_backend_record = {};
		active_scene_owner = {};
		reset_call_stack_keys();
		for (std::size_t index{}; index < api_count; ++index)
		{
			per_api[index].store(0, std::memory_order_relaxed);
			classifier_per_api[index].store(0, std::memory_order_relaxed);
			frame_per_api[index].store(0, std::memory_order_relaxed);
		}
		classifier_record.store(0, std::memory_order_relaxed);
		classifier_record_type.store(0, std::memory_order_relaxed);
		classifier_thread_id.store(0, std::memory_order_relaxed);
		classifier_begin_qpc.store(0, std::memory_order_relaxed);
		classifier_end_qpc.store(0, std::memory_order_relaxed);
		frame_start_present_post.store(0, std::memory_order_relaxed);
		frame_end_present_pre.store(0, std::memory_order_relaxed);
		frame_end_present_post.store(0, std::memory_order_relaxed);
		frame_present_result.store(0, std::memory_order_relaxed);
		frame_start_thread_id.store(0, std::memory_order_relaxed);
		frame_end_thread_id.store(0, std::memory_order_relaxed);
		observation_context.store(0, std::memory_order_relaxed);
		observation_generation.store(0, std::memory_order_relaxed);
		current_failure.store(failure::none, std::memory_order_relaxed);
		failure_requested.store(false, std::memory_order_relaxed);
		finalizer_claimed.store(false, std::memory_order_relaxed);
		classifier_owner_active.store(false, std::memory_order_relaxed);
		report_ready.store(false, std::memory_order_relaxed);
#if defined(H2VR_EXECUTION_PROBE_TESTING)
		admission_pause_stage.store(0, std::memory_order_relaxed);
		admission_pause_entered.store(false, std::memory_order_relaxed);
		admission_pause_released.store(false, std::memory_order_relaxed);
#endif
		std::memset(std::addressof(published_report), 0,
			sizeof(published_report));
		current_state.store(gate_state::classifier_pending, std::memory_order_release);
		return true;
	}
}
