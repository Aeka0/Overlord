#pragma once

#include "engine_stereo_output_merger.hpp"

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_execution
{
	inline constexpr std::size_t maximum_events = 32768;
	inline constexpr std::size_t maximum_call_stack_depth = 16;

	enum class gate_state : std::uint8_t
	{
		classifier_pending,
		classifier_arming,
		classifier_active,
		classifier_end_arming,
		classifier_closing,
		frame_pending,
		frame_arming,
		frame_active,
		frame_end_arming,
		awaiting_end_present_post,
		end_present_arming,
		frame_closing,
		failure_closing,
		finalizing,
		resetting,
		complete,
		failed,
	};

	enum class api : std::uint8_t
	{
		draw_indexed,
		draw,
		draw_indexed_instanced,
		draw_instanced,
		draw_auto,
		draw_indexed_instanced_indirect,
		draw_instanced_indirect,
		dispatch,
		dispatch_indirect,
		execute_command_list,
		count,
	};

	inline constexpr std::size_t api_count = static_cast<std::size_t>(api::count);
	using invocation_observer_fn = void(*)(ID3D11DeviceContext*, api,
		std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint64_t,
		std::uint8_t, const std::array<std::uint64_t, 6>&) noexcept;
	enum class invocation_observer_channel : std::uint8_t
	{
		gpu_census,
		ssr_consumer,
		effect_timeline,
		count,
	};
	inline constexpr std::size_t invocation_observer_channel_count =
		static_cast<std::size_t>(invocation_observer_channel::count);
	using opaque_state_observer_fn = void(*)(ID3D11DeviceContext*,
		ID3D11CommandList*, bool, std::uintptr_t) noexcept;
	using draw_indexed_observer_fn = void(*)(ID3D11DeviceContext*,
		std::uintptr_t, std::uint32_t) noexcept;
	inline constexpr std::uint8_t classifier_scope_flag = 1u << 0;
	inline constexpr std::uint8_t frame_scope_flag = 1u << 1;

	enum class failure : std::uint8_t
	{
		none,
		install,
		device_invalidated,
		classifier_contract,
		classifier_collision,
		admission_collision,
		frame_boundary,
		present_failed,
		present_owner_mismatch,
		device_generation,
		overflow,
		identity_truncation,
	};

	// CPU-only provenance published by H2's already-observed backend record
	// lifetime. This contains pointer/integer identities only; the execution hooks
	// never dereference these values or change backend ownership.
	struct backend_record_context
	{
		std::uint64_t backend_id{};
		std::uint64_t frontend_epoch{};
		std::uint64_t frontend_transaction_id{};
		std::uintptr_t record{};
		std::uintptr_t frontend{};
		std::uintptr_t command_stream{};
		std::uint32_t record_index{};
		std::uint32_t record_type{};
		std::uint32_t target_id{};
		std::uint32_t owner_thread_id{};
		bool dispatch_entered{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return backend_id != 0 && record != 0;
		}
	};

	// Exact CPU lifetime of H2's outer current-record renderer at
	// 0x1407A7DA0..0x1407A8306. This is independent from the narrower nested
	// backend record/command transaction above: both may be active on one event.
	// Pointer and target identities are copied only and are never dereferenced by
	// the D3D observer.
	struct scene_owner_context
	{
		std::uint64_t observation_id{};
		std::uintptr_t record{};
		std::uintptr_t frontend{};
		std::uintptr_t caller{};
		std::uint32_t record_index{};
		std::uint32_t record_type{};
		std::array<std::uint32_t, 3> target_ids{};
		std::uint32_t target_selector{};
		std::uint32_t owner_thread_id{};
		bool record_valid{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return observation_id != 0 && record != 0;
		}
	};

	struct event
	{
		std::uint64_t sequence{};
		std::uint64_t timestamp_qpc{};
		std::uintptr_t context{};
		std::uintptr_t caller{};
		std::uint32_t thread_id{};
		api operation{api::draw_indexed};
		std::uint8_t scope_flags{};
		std::uint8_t argument_count{};
		bool expected_context{};
		std::array<std::uint64_t, 6> arguments{};
		engine_stereo_output_merger::binding_snapshot output_binding{};
		backend_record_context backend{};
		scene_owner_context scene_owner{};
		std::uint8_t call_stack_depth{};
		bool backend_thread_match{};
		bool scene_owner_thread_match{};
		std::array<std::uintptr_t, maximum_call_stack_depth> call_stack{};
	};

	struct report
	{
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uintptr_t classifier_record{};
		std::uint32_t classifier_record_type{};
		std::uint32_t classifier_thread_id{};
		std::uint64_t classifier_begin_qpc{};
		std::uint64_t classifier_end_qpc{};
		std::uint64_t frame_start_present_post{};
		std::uint64_t frame_end_present_pre{};
		std::uint64_t frame_end_present_post{};
		std::int32_t frame_present_result{};
		std::uint32_t frame_start_thread_id{};
		std::uint32_t frame_end_thread_id{};
		std::uint32_t event_count{};
		std::uint32_t classifier_event_count{};
		std::uint32_t frame_event_count{};
		std::uint32_t overflow_count{};
		std::uint32_t foreign_context_events{};
		std::uint32_t expected_context_frame_events{};
		std::uint32_t backend_scoped_events{};
		std::uint32_t backend_scoped_frame_events{};
		std::uint32_t backend_unscoped_frame_events{};
		std::uint32_t backend_thread_mismatches{};
		std::uint32_t distinct_backend_records{};
		std::uint32_t scene_owner_scoped_events{};
		std::uint32_t scene_owner_scoped_frame_events{};
		std::uint32_t scene_owner_unscoped_frame_events{};
		std::uint32_t scene_owner_thread_mismatches{};
		std::uint32_t distinct_scene_owner_records{};
		std::uint32_t call_stack_samples{};
		std::uint32_t call_stack_capture_failures{};
		std::uint32_t call_stack_key_overflows{};
		std::uint32_t admission_collisions{};
		std::uint32_t classifier_thread_mismatches{};
		std::uint32_t distinct_contexts{};
		std::uint32_t distinct_threads{};
		std::uint32_t opaque_execute_command_lists{};
		std::uint32_t known_conversion_recordings{},known_conversion_executions{},known_conversion_replays{};
		bool deferred_execution_opaque{};
		bool identity_truncated{};
		std::array<std::uint32_t, api_count> per_api{};
		std::array<std::uint32_t, api_count> classifier_per_api{};
		std::array<std::uint32_t, api_count> frame_per_api{};
		std::array<event, maximum_events> events{};
	};

	struct classifier_scope
	{
		bool active{};
		std::uintptr_t record{};
		std::uint32_t record_type{};
		std::uint32_t thread_id{};
		std::uint64_t begin_qpc{};
	};

	struct invocation_scope
	{
		bool active{};
		bool event_recorded{};
		bool known_conversion{};
		std::uint32_t event_index{};
	};

	struct status
	{
		gate_state state{gate_state::classifier_pending};
		failure error{failure::none};
		bool hooks_installed{};
		bool draw_indexed_forwarding_external{true};
		bool report_ready{};
		bool targets_distinct{};
		bool installation_permanently_failed{};
		bool deferred_context_probe_attempted{};
		bool deferred_context_probe_succeeded{};
		bool deferred_execution_opaque{};
		bool identity_truncated{};
		std::uint32_t installed_hook_count{};
		std::array<std::uintptr_t, api_count> hook_targets{};
		std::uintptr_t output_merger_target{};
		std::uintptr_t output_merger_unordered_access_target{};
		std::uintptr_t clear_state_target{};
		std::array<bool, api_count> deferred_target_matches{};
		bool deferred_output_merger_target_matches{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint64_t hook_failures{};
		std::uint64_t attempts{};
		std::uint64_t completions{};
		std::uint64_t failures{};
		std::uint32_t event_reservations{};
		std::uint32_t recorded_events{};
		std::uint32_t classifier_events{};
		std::uint32_t frame_events{};
		std::uint32_t overflows{};
		std::uint32_t foreign_context_events{};
		std::uint32_t expected_context_frame_events{};
		std::uint32_t backend_scoped_events{};
		std::uint32_t backend_scoped_frame_events{};
		std::uint32_t backend_unscoped_frame_events{};
		std::uint32_t backend_thread_mismatches{};
		std::uint32_t distinct_backend_records{};
		std::uint32_t scene_owner_scoped_events{};
		std::uint32_t scene_owner_scoped_frame_events{};
		std::uint32_t scene_owner_unscoped_frame_events{};
		std::uint32_t scene_owner_thread_mismatches{};
		std::uint32_t distinct_scene_owner_records{};
		std::uint32_t call_stack_samples{};
		std::uint32_t call_stack_capture_failures{};
		std::uint32_t call_stack_key_overflows{};
		std::uint32_t admission_collisions{};
		std::uint32_t classifier_thread_mismatches{};
		std::uint32_t active_writers{};
		std::uint32_t maximum_active_writers{};
		std::uint32_t distinct_contexts{};
		std::uint32_t distinct_threads{};
		std::uint32_t draw_indexed_forwarded_calls{};
		std::uint32_t opaque_execute_command_lists{};
		std::uint32_t known_conversion_recordings{},known_conversion_executions{},known_conversion_replays{};
		std::array<std::uint32_t, api_count> per_api{};
		std::array<std::uint32_t, api_count> classifier_per_api{};
		std::array<std::uint32_t, api_count> frame_per_api{};
		std::uintptr_t classifier_record{};
		std::uint32_t classifier_record_type{};
		std::uint32_t classifier_thread_id{};
		std::uint64_t classifier_begin_qpc{};
		std::uint64_t classifier_end_qpc{};
		std::uint64_t frame_start_present_post{};
		std::uint64_t frame_end_present_pre{};
		std::uint64_t frame_end_present_post{};
		std::int32_t frame_present_result{};
		std::uint32_t frame_start_thread_id{};
		std::uint32_t frame_end_thread_id{};
	};

	// Installs process-wide observers at the active context implementation. The
	// observers always call the natural D3D11 method exactly once and never issue
	// additional GPU work. DrawIndexed remains owned by the existing observer and
	// forwards the exact natural invocation through begin/end_draw_indexed().
	[[nodiscard]] bool install(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;

	// The exact H2 classifier is captured first. Once it closes, the next
	// successful Present-post begins a separate complete Present-to-Present census.
	[[nodiscard]] bool begin_classifier(classifier_scope& output,
		std::uintptr_t record, std::uint32_t record_type) noexcept;
	[[nodiscard]] bool end_classifier(classifier_scope& active) noexcept;

	// Called only by the existing H2 backend-record observers on their owner
	// thread. The context is copied into subsequent admitted D3D events and is
	// cleared at the exact record boundary; it does not affect admission or GPU
	// execution.
	void set_backend_record_context(const backend_record_context& context) noexcept;
	void clear_backend_record_context() noexcept;
	void set_scene_owner_context(const scene_owner_context& context) noexcept;
	void clear_scene_owner_context() noexcept;
	// Optional read-only observer for bounded diagnostics. Production execution
	// remains independent and does not require an observer to be registered.
	void set_invocation_observer(invocation_observer_channel channel,
		invocation_observer_fn observer) noexcept;
	// Independent from the bounded invocation observer so a diagnostic that
	// tracks immediate-context state cannot be displaced by the GPU census.
	// ExecuteCommandList is opaque to that tracker and is the only event routed
	// through this callback.
	void set_opaque_state_observer(opaque_state_observer_fn observer) noexcept;
	void set_draw_indexed_observer(draw_indexed_observer_fn observer) noexcept;

	void begin_draw_indexed(invocation_scope& output,
		ID3D11DeviceContext* context, UINT index_count,
		UINT start_index_location, INT base_vertex_location,
		std::uintptr_t caller) noexcept;
	void end_draw_indexed(invocation_scope& active) noexcept;
	void on_present_pre(std::uint64_t frame_index,
		std::uint64_t device_generation, std::uint32_t thread_id) noexcept;
	void on_present_post(std::uint64_t frame_index,
		std::uint64_t device_generation, std::uint32_t thread_id,
		HRESULT result) noexcept;

	// A completed one-shot census must no longer consume frontend publications.
	// These belong exclusively to the real stereo scene owner after bootstrap.
	[[nodiscard]] bool bootstrap_complete() noexcept;
	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] bool read_report(report& output) noexcept;
	[[nodiscard]] const char* to_string(gate_state state) noexcept;
	[[nodiscard]] const char* to_string(api value) noexcept;
	[[nodiscard]] const char* to_string(failure value) noexcept;
	// Control-plane/test only. Refuses to reset while either scope has writers.
	[[nodiscard]] bool reset() noexcept;

#if defined(H2VR_EXECUTION_PROBE_TESTING)
	// Deterministic offline race control. Never compiled into the game client.
	enum class admission_test_stage : std::uint8_t
	{
		after_gate_claim = 1,
		after_state_check = 2,
		after_gate_open = 3,
	};
	void test_arm_admission_pause(admission_test_stage stage) noexcept;
	[[nodiscard]] bool test_admission_pause_entered() noexcept;
	void test_release_admission_pause() noexcept;
#endif
}
