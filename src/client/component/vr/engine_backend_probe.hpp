#pragma once

#include "component/vr/native_render_contract.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace vr::engine_backend_probe
{

	inline constexpr std::size_t frontend_publication_capacity = 64;
	inline constexpr std::size_t query_publication_capacity = 64;
	inline constexpr std::size_t trace_capacity = 16 * 1024;

	enum class trace_domain : std::uint8_t
	{
		backend = 1,
		query = 2,
		target_prepare = 3,
	};

	enum class event_kind : std::uint8_t
	{
		backend_begin = 1,
		backend_post_bind,
		backend_target,
		backend_dispatch,
		backend_end,
		query_publish,
		query_result,
		present_result,
		target_prepare,
	};

	enum class observation_stage : std::uint8_t
	{
		snapshot,
		before_call,
		after_call,
		leave,
	};

	enum class backend_completion : std::uint8_t
	{
		none,
		cpu_dispatch_return,
		dispatch_skipped_null,
		incomplete,
	};

	enum class backend_watchdog_phase : std::uint8_t
	{
		idle,
		backend_entered,
		post_bind,
		dispatch_before_call,
		dispatch_after_call,
		leaving,
	};

	enum class query_result_class : std::uint8_t
	{
		identity_mismatch,
		pending,
		complete,
		failed,
	};

	enum class backend_fault : std::uint32_t
	{
		none = 0,
		mapping_miss = 1u << 0,
		record_mismatch = 1u << 1,
		thread_mismatch = 1u << 2,
		record_type_mismatch = 1u << 3,
		target_invalid = 1u << 4,
		orphan_dispatch = 1u << 5,
		overlap = 1u << 6,
		command_mismatch = 1u << 7,
		target_registry_unavailable = 1u << 8,
	};

	using backend_faults = std::uint32_t;

	[[nodiscard]] constexpr backend_faults fault(const backend_fault value) noexcept
	{
		return static_cast<backend_faults>(value);
	}

	[[nodiscard]] constexpr backend_faults with_fault(const backend_faults values,
		const backend_fault value) noexcept
	{
		return values | fault(value);
	}

	[[nodiscard]] constexpr bool has_fault(const backend_faults values,
		const backend_fault value) noexcept
	{
		return (values & fault(value)) != 0;
	}

	[[nodiscard]] constexpr std::uint64_t pack_u32_pair(const std::uint32_t low,
		const std::uint32_t high) noexcept
	{
		return static_cast<std::uint64_t>(low) |
			(static_cast<std::uint64_t>(high) << 32);
	}

	[[nodiscard]] constexpr std::uint32_t unpack_low_u32(const std::uint64_t value) noexcept
	{
		return static_cast<std::uint32_t>(value);
	}

	[[nodiscard]] constexpr std::uint32_t unpack_high_u32(const std::uint64_t value) noexcept
	{
		return static_cast<std::uint32_t>(value >> 32);
	}

	struct frontend_record_observation
	{
		std::uintptr_t frontend{};
		std::uintptr_t record{};
		std::uint32_t record_index{};
		std::uint32_t record_type{};
		std::uint64_t frontend_epoch{};
		std::uint64_t frontend_transaction_id{};
	};

	struct target_snapshot
	{
		std::uintptr_t entry{};
		std::uintptr_t qword_0{};
		std::uintptr_t qword_1{};
		std::uintptr_t qword_2{};
		std::uintptr_t qword_3{};
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t related_target{};
		std::array<std::uint32_t, 3> record_target_ids{};
		std::uint32_t record_pingpong_selector{};
	};

	struct target_prepare_observation
	{
		std::uintptr_t frontend{};
		std::uintptr_t record{};
		std::uint32_t record_index{};
		std::uint32_t record_type{};
		std::uint32_t changed_bytes{};
		std::uint32_t first_changed{};
		std::uint32_t last_changed{};
		std::uint64_t changed_block_mask{};
		std::array<std::uint32_t, 3> targets_before{};
		std::array<std::uint32_t, 3> targets_after{};
		std::uint32_t selector_before{};
		std::uint32_t selector_after{};
		bool record_valid{};
		std::uintptr_t caller{};
	};

	struct backend_token
	{
		std::uint64_t backend_id{};
		std::uint64_t trace_id{};
		std::uint64_t probe_generation{};
		std::uint64_t watchdog_sequence{};
		std::uint64_t frontend_epoch{};
		std::uint64_t frontend_transaction_id{};
		std::uint64_t publication_sequence{};
		std::uint64_t started_qpc{};
		std::uint64_t dispatch_started_qpc{};
		std::uintptr_t record{};
		std::uintptr_t frontend{};
		std::uintptr_t command_stream{};
		std::uint32_t record_index{};
		std::uint32_t record_type{};
		std::uint32_t target_id{};
		std::uint32_t owner_thread_id{};
		std::uint16_t depth{1};
		backend_faults faults{};
		bool dispatch_entered{};
		bool dispatch_returned{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return backend_id != 0 && trace_id != 0;
		}
	};

	struct trace_record
	{
		std::uint64_t sequence{};
		std::uint64_t timestamp_qpc{};
		std::uint64_t trace_id{};
		std::uint64_t frontend_epoch{};
		std::uint32_t thread_id{};
		std::uint16_t depth{};
		event_kind kind{event_kind::backend_begin};
		observation_stage stage{observation_stage::snapshot};
		std::array<std::uint64_t, 8> values{};
	};

	// This snapshot is observation-only.  depth==0 is the sole idle marker;
	// sequence and the remaining fields are deliberately retained so a report can
	// identify the last backend transaction after it leaves.
	struct backend_watchdog_status
	{
		std::uint64_t sequence{};
		std::uint64_t entered_tick{};
		std::uint64_t last_progress_tick{};
		std::uint32_t thread_id{};
		std::uint32_t depth{};
		std::uintptr_t record{};
		std::uintptr_t command_stream{};
		backend_watchdog_phase phase{backend_watchdog_phase::idle};
	};

	struct status
	{
		bool enabled{};
		std::uint64_t probe_generation{};
		std::uint64_t newest_trace_sequence{};
		std::uint64_t trace_overwrite_count{};
		std::uint64_t trace_drop_count{};

		std::uint64_t frontend_publications{};
		std::uint64_t frontend_claims{};
		std::uint64_t frontend_mapping_misses{};
		std::uint64_t frontend_publication_drops{};
		std::uint64_t frontend_unclaimed_overwrites{};

		std::uint64_t backend_transactions{};
		std::uint64_t backend_post_binds{};
		std::uint64_t backend_dispatch_enters{};
		std::uint64_t backend_cpu_dispatch_returns{};
		std::uint64_t backend_dispatch_skipped_null{};
		std::uint64_t backend_incomplete{};
		std::uint64_t backend_active{};
		std::uint64_t backend_maximum_active{};
		std::uint64_t backend_orphan_dispatches{};
		std::uint64_t backend_record_mismatches{};
		std::uint64_t backend_thread_mismatches{};
		std::uint64_t backend_record_type_mismatches{};
		std::uint64_t backend_target_invalid{};
		std::uint64_t backend_command_mismatches{};
		std::uint64_t target_prepare_calls{};
		std::uint64_t target_prepare_valid_records{};
		std::uint64_t target_prepare_invalid_records{};
		std::uint64_t target_prepare_changed_records{};
		std::uint64_t target_prepare_target_changes{};

		std::uint64_t query_publications{};
		std::uint64_t query_publication_drops{};
		std::uint64_t query_unresolved_overwrites{};
		std::uint64_t query_results{};
		std::uint64_t query_pending_results{};
		std::uint64_t query_complete_results{};
		std::uint64_t query_failed_results{};
		std::uint64_t query_identity_mismatches{};
		std::uint64_t query_generation_mismatches{};
		std::uint64_t query_device_mismatches{};
		std::uint64_t query_stale_generations{};
		std::uint64_t query_present_prerequisite_misses{};
		// A completed query is only diagnostic evidence.  This probe intentionally
		// has no resource-owner binding and therefore cannot retire anything.
		std::uint64_t query_unbound_completions{};
		std::uint64_t present_results{};
		std::uint64_t current_device_generation{};
		std::uint64_t latest_query_generation{};
		std::uint64_t last_present_frame_index{};
		std::uint64_t last_present_device_generation{};
		std::uint32_t last_present_thread_id{};
		std::int32_t last_present_result{};

		std::uint64_t last_backend_id{};
		std::uint64_t last_frontend_epoch{};
		std::uint64_t last_frontend_transaction_id{};
		std::uintptr_t last_record{};
		std::uintptr_t last_frontend{};
		std::uintptr_t last_command_stream{};
		std::uint32_t last_record_index{};
		std::uint32_t last_record_type{};
		std::uint32_t last_target_id{};
		std::uint32_t last_backend_thread_id{};
		std::uint64_t last_dispatch_duration_qpc{};
		std::uint64_t last_query_generation{};
		std::uintptr_t last_query_identity{};
		std::int32_t last_query_result{};
		bool last_query_complete{};
	};

	void set_enabled(bool enabled) noexcept;
	[[nodiscard]] bool is_enabled() noexcept;
	// Control-plane only.  Reset first disables the probe, then waits for every
	// observer callback and any complete backend transaction to leave.  It clears
	// storage only after quiescence; on timeout it returns false and deliberately
	// leaves the probe disabled with all prior evidence intact.
	[[nodiscard]] bool reset() noexcept;
	// Copy one fingerprint-gated H2 target-registry entry without interpreting its
	// qwords. Callers that need a coherent observation must read twice and compare.
	[[nodiscard]] bool read_target_registry_entry(std::uint32_t target_id,
		native_render_contract::target_registry_entry& output) noexcept;

	// Publish only after the one natural H2 R_RenderScene call has returned.
	[[nodiscard]] bool publish_frontend_record(
		const frontend_record_observation& observation) noexcept;

	// begin_backend claims the oldest unclaimed frontend publication matching the
	// exact record/frontend identity.  A miss remains observable but never changes
	// H2 execution.
	[[nodiscard]] backend_token begin_backend(std::uintptr_t record) noexcept;
	void record_post_bind(backend_token& token, std::uintptr_t record,
		std::uint32_t target_id) noexcept;
	void record_dispatch(backend_token& token, std::uintptr_t commands,
		observation_stage stage) noexcept;
	void end_backend(backend_token& token, backend_completion completion,
		std::uintptr_t commands = 0, backend_faults additional_faults = 0) noexcept;
	void record_target_prepare(const target_prepare_observation& observation) noexcept;

	// generation_before/generation_after describe the query-publication counter,
	// not a backend token.  A reused query pointer is disambiguated by the exact
	// (identity, generation_before) tuple.
	[[nodiscard]] bool record_query_publish(std::uintptr_t query,
		std::uint64_t generation_before, std::uint64_t generation_after,
		std::uint64_t frontend_epoch) noexcept;
	[[nodiscard]] query_result_class record_query_result(std::uintptr_t query,
		std::uint64_t generation, std::int32_t result, bool complete) noexcept;

	// The query stream is hard-unarmed while device_generation is zero.  A query
	// publication snapshots only the most recent Present recorded on that same
	// thread and device generation.  These APIs never call Present/GetData.
	void set_device_generation(std::uint64_t device_generation) noexcept;
	void record_present_result(std::uint64_t device_generation,
		std::uint64_t frame_index, std::int32_t result) noexcept;

	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] backend_watchdog_status get_backend_watchdog_status() noexcept;
	[[nodiscard]] std::size_t read_recent(trace_record* output,
		std::size_t capacity) noexcept;
	// Control-plane formatting may allocate.  Never call from an H2 renderer or
	// query-result hook.
	[[nodiscard]] std::string format_recent(std::size_t maximum_records = 256);
	[[nodiscard]] const char* to_string(event_kind value) noexcept;
	[[nodiscard]] const char* to_string(observation_stage value) noexcept;
	[[nodiscard]] const char* to_string(backend_completion value) noexcept;
	[[nodiscard]] const char* to_string(backend_watchdog_phase value) noexcept;
	[[nodiscard]] const char* to_string(query_result_class value) noexcept;
}
