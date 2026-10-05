#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_scene_batch_probe
{
	inline constexpr std::size_t backend_data_offset = 0x2BE8;
	inline constexpr std::size_t descriptor_array_offset = 0x541C08;
	inline constexpr std::size_t descriptor_count_offset = 0x542588;
	inline constexpr std::size_t descriptor_size = 0x130;
	inline constexpr std::size_t maximum_descriptors = 8;
	inline constexpr std::size_t stream_count = 12;
	inline constexpr std::size_t maximum_boundaries_per_eye = 64;
	inline constexpr std::size_t maximum_mismatch_samples = 32;
	inline constexpr std::size_t local_complex_state_size = 0x1BD0;
	inline constexpr std::size_t local_simple_state_size = 0x310;
	inline constexpr std::size_t local_complex_maximum_groups = 9;
	inline constexpr std::size_t local_complex_maximum_entries = 99;
	inline constexpr std::size_t local_simple_maximum_entries = 11;
	inline constexpr std::size_t local_consumer_count = 4;
	inline constexpr std::size_t maximum_local_samples_per_eye = 64;
	inline constexpr std::size_t maximum_local_gate_samples_per_eye = 64;
	inline constexpr std::size_t maximum_gate_samples_per_consumer =
		maximum_local_gate_samples_per_eye / local_consumer_count;
	static_assert(maximum_local_gate_samples_per_eye % local_consumer_count == 0);
	inline constexpr std::size_t local_hook_count = 11;

	inline constexpr std::uintptr_t ssr_consumer = 0x140797D10ull;
	inline constexpr std::uintptr_t spark_consumer = 0x140798170ull;
	inline constexpr std::uintptr_t code_trans_consumer = 0x140798210ull;
	inline constexpr std::uintptr_t glass_consumer = 0x140798430ull;
	// This observer runs synchronously on H2's render-owner thread for one pair.
	// Four 1 KiB windows retain broad payload coverage without turning the census
	// itself into a multi-frame stall.
	inline constexpr std::size_t maximum_hashed_bytes_per_stream = 4 * 1024;

	enum class state : std::uint8_t
	{
		idle,
		armed,
		recording,
		complete,
	};

	enum class mismatch_kind : std::uint8_t
	{
		boundary,
		descriptor_count,
		backend_data,
		descriptor,
		stream_pointer,
		stream_count,
		stream_content,
		unreadable,
	};

	enum class local_executor_kind : std::uint8_t
	{
		complex,
		simple,
	};

	// Indices are also the required order of report::hook_targets and
	// report::hook_callsites_observed.
	enum class local_hook_kind : std::uint8_t
	{
		primary,
		simple_executor,
		complex_executor,
		ssr_gate,
		code_trans_gate,
		glass_gate,
		spark_gate,
		ssr_second_pass,
		code_trans_second_pass,
		glass_second_pass,
		spark_second_pass,
	};

	enum class local_consumer_kind : std::uint8_t
	{
		ssr,
		code_trans,
		glass,
		spark,
	};

	enum class observer_stage : std::uint8_t
	{
		boundary, primary, executor_begin, executor_end, gate, second_pass, finish, count,
	};

	struct timing_sample
	{
		std::uint64_t calls{};
		std::uint64_t total_ns{};
		std::uint64_t maximum_ns{};
	};

	struct primary_token
	{
		std::uint64_t cookie{};
		std::uint32_t slot{0xFFFFFFFFu};
		bool active{};
	};

	struct executor_token
	{
		std::uint64_t cookie{};
		std::uint32_t slot{0xFFFFFFFFu};
		local_executor_kind kind{local_executor_kind::complex};
		bool active{};
	};

	struct local_consumer_eye_report
	{
		std::uint64_t calls{};
		std::uint64_t entries{};
		std::uint64_t cursor_advances{};
		std::uint64_t cursor_unchanged{};
		std::uint64_t cursor_regressions{};
		std::uint64_t cursor_advance_bytes{};
		std::uint64_t unresolved_sources{};
		std::uint64_t source_shape_mismatches{};
		std::uint64_t payload_hashes{};
		std::uint64_t payload_hash_failures{};
		std::uint64_t payload_sampled_bytes{};
		std::uint64_t gate_calls{};
		std::uint64_t gate_true{};
		std::uint64_t gate_false{};
		std::uint64_t gate_context_failures{};
		std::uint64_t gate_samples{};
		std::uint64_t gate_samples_dropped{};
		std::uint64_t second_pass_calls{};
		std::uintptr_t latest_source_descriptor{};
		std::uint32_t latest_source_count{};
		std::uintptr_t latest_source_pointer{};
		std::uint64_t latest_source_byte_count{};
		std::uint64_t latest_source_content_hash{};
		std::uint32_t latest_source_sampled_bytes{};
		std::uint8_t latest_source_sampled_windows{};
		std::uintptr_t latest_cursor_pre{};
		std::uintptr_t latest_cursor_end{};
		std::uintptr_t latest_cursor_post{};
		std::uintptr_t latest_gate_context{};
		std::uint64_t latest_gate_key{};
		std::uintptr_t latest_gate_secondary{};
		std::uintptr_t latest_gate_technique{};
		std::uint16_t latest_gate_pass_count{};
		std::uintptr_t latest_second_pass_context{};
		std::uintptr_t latest_second_pass_return{};
	};

	struct local_consumer_report
	{
		std::uintptr_t consumer{};
		std::array<local_consumer_eye_report, 2> eyes{};
	};

	struct local_sample
	{
		local_executor_kind executor{local_executor_kind::complex};
		local_consumer_kind consumer{local_consumer_kind::ssr};
		std::uint32_t eye{};
		std::uint32_t call_ordinal{};
		std::uint32_t entry_index{};
		std::uint32_t group_index{};
		std::uintptr_t return_address{};
		std::uintptr_t source_descriptor{};
		std::uint32_t source_count{};
		std::uintptr_t source_pointer{};
		std::uint64_t source_byte_count{};
		std::uint64_t source_content_hash{};
		std::uint32_t source_sampled_bytes{};
		std::uint8_t source_sampled_windows{};
		bool source_content_readable{};
		std::uintptr_t cursor_pre{};
		std::uintptr_t cursor_end{};
		std::uintptr_t cursor_post{};
	};

	// Fixed, read-only post-gate selection. Layouts match H2 MaterialInfo,
	// MaterialTechniqueHeader and MaterialPass; this is not GPU binding evidence.
	struct material_selection_snapshot
	{
		std::uintptr_t material{};
		std::uint32_t technique_type{};
		std::uintptr_t pass{};
		std::uint32_t pass_index{};
		std::array<std::uint8_t, 4> atlas{}; // rows, columns, frame blend, as-array
		std::array<std::uint8_t, 0x48> pass_bytes{};
		std::uint32_t state_bits_index{};
		std::array<std::uint8_t, 0x28> state_bits{};
		std::array<char, 64> material_name{};
		std::array<char, 64> technique_name{};
		bool readable{};
		bool pass_address_matches{};
		bool state_bits_readable{};
		bool material_name_readable{};
		bool technique_name_readable{};
	};

	struct local_gate_sample
	{
		local_consumer_kind consumer{local_consumer_kind::ssr};
		std::uint32_t eye{};
		std::uint32_t call_ordinal{};
		bool accepted{};
		bool context_readable{};
		std::uintptr_t return_address{};
		std::uintptr_t context{};
		std::uint64_t key{};
		std::array<std::uintptr_t, 4> context_words{};
		std::uintptr_t secondary{};
		std::uintptr_t technique{};
		std::uint16_t pass_count{};
		material_selection_snapshot selection{};
	};

	struct mismatch_sample
	{
		mismatch_kind kind{mismatch_kind::boundary};
		std::uint32_t boundary_ordinal{};
		std::uint32_t descriptor_index{0xFFFFFFFFu};
		std::uint32_t stream_index{0xFFFFFFFFu};
		std::array<std::uintptr_t, 2> addresses{};
		std::array<std::uint64_t, 2> values{};
		std::array<std::uint64_t, 2> byte_counts{};
	};

	struct report
	{
		state current{state::idle};
		std::uint64_t pair_id{};
		std::uint32_t owner_thread{};
		std::uint32_t active_eye{2};
		std::uint8_t completed_eye_mask{};
		bool owner_complete{};
		// UTC milliseconds correlate with external runtime logs. Durations use a
		// monotonic clock. Observer timings exclude the wrapped original H2 calls;
		// read/query timings are nested subsets and must not be added to them.
		std::uint64_t armed_utc_ms{};
		std::uint64_t started_utc_ms{};
		std::uint64_t completed_utc_ms{};
		std::uint64_t arm_ns{};
		std::uint64_t pair_wall_ns{};
		std::array<std::uint64_t, 2> eye_wall_ns{};
		std::array<std::uint64_t, 2> eye_observer_ns{};
		std::array<timing_sample, static_cast<std::size_t>(observer_stage::count)>
			observer_timings{};
		timing_sample observer_lock_wait{};
		timing_sample guarded_read_timing{};
		timing_sample virtual_query_timing{};

		std::uint64_t arm_attempts{};
		std::uint64_t pair_attempts{};
		std::uint64_t pair_completions{};
		std::uint64_t lifecycle_mismatches{};
		std::array<std::uint32_t, 2> observations{};
		std::array<std::uint32_t, 2> unique_boundaries{};
		std::uint64_t boundary_overflows{};
		std::uint64_t boundary_duplicates{};
		std::uint64_t boundary_mismatches{};
		std::uint64_t compared_boundaries{};
		std::uint64_t descriptor_count_mismatches{};
		std::uint64_t backend_data_mismatches{};
		std::uint64_t descriptor_mismatches{};
		std::uint64_t compared_descriptors{};
		std::uint64_t stream_pointer_mismatches{};
		std::uint64_t stream_count_mismatches{};
		std::uint64_t stream_content_mismatches{};
		std::uint64_t compared_streams{};
		std::uint64_t unreadable_sources{};
		std::uint64_t sampled_payload_bytes{};

		std::array<std::uintptr_t, 2> owner_records{};
		std::array<std::uintptr_t, 2> backend_states{};
		std::array<std::uintptr_t, 2> latest_backend_data{};
		std::array<std::uint32_t, 2> latest_descriptor_counts{};
		std::array<mismatch_sample, maximum_mismatch_samples> samples{};
		std::size_t sample_count{};
		std::uint64_t dropped_samples{};

		bool hooks_installed{};
		std::uint64_t hook_install_attempts{};
		std::uint64_t hook_install_failures{};
		std::array<std::uintptr_t, local_hook_count> hook_targets{};
		std::array<bool, local_hook_count> hook_callsites_observed{};
		std::array<std::uint64_t, 2> primary_calls{};
		std::array<std::uint64_t, 2> primary_source_mutations{};
		std::array<std::uint64_t, 2> primary_pair16_mutations{};
		std::array<std::uint64_t, 2> complex_executor_calls{};
		std::array<std::uint64_t, 2> simple_executor_calls{};
		std::array<std::uint64_t, 2> complex_group_counts{};
		std::array<std::uint64_t, 2> complex_entry_counts{};
		std::array<std::uint64_t, 2> simple_entry_counts{};
		std::uint64_t local_parse_failures{};
		std::uint64_t local_capacity_overflows{};
		std::uint64_t local_lifecycle_mismatches{};
		std::uint64_t local_payload_comparisons{};
		std::uint64_t local_payload_mismatches{};
		std::uint64_t local_payload_unreadable{};
		std::uint64_t local_payload_pair_misses{};
		std::uint64_t local_source_pointer_mismatches{};
		std::uint64_t local_source_count_mismatches{};
		std::uint64_t local_gate_comparisons{};
		std::uint64_t local_gate_result_mismatches{};
		std::uint64_t local_gate_key_mismatches{};
		std::uint64_t local_gate_pass_count_mismatches{};
		std::uint64_t selection_comparisons{};
		std::uint64_t selection_unreadable{};
		std::uint64_t selection_identity_mismatches{};
		std::uint64_t selection_pass_mismatches{};
		std::uint64_t selection_atlas_mismatches{};
		std::uint64_t selection_state_comparisons{};
		std::uint64_t selection_state_unreadable{};
		std::uint64_t selection_state_mismatches{};
		std::uint64_t local_gate_context_unreadable{};
		std::uint64_t local_gate_pair_misses{};
		std::array<local_consumer_report, local_consumer_count> local_consumers{};
		std::array<std::array<local_sample, maximum_local_samples_per_eye>, 2>
			local_samples{};
		std::array<std::size_t, 2> local_sample_counts{};
		std::array<std::uint64_t, 2> dropped_local_samples{};
		std::array<std::array<local_gate_sample,
			maximum_local_gate_samples_per_eye>, 2> local_gate_samples{};
		std::array<std::size_t, 2> local_gate_sample_counts{};
		std::array<std::uint64_t, 2> dropped_local_gate_samples{};
	};

	// Replaces a completed/idle observation with a fresh one-shot. It refuses to
	// disturb an executing pair. The return value means only that the observer was
	// armed; it is never a render-validity or production-acceptance verdict.
	[[nodiscard]] bool arm() noexcept;
	[[nodiscard]] bool begin_pair(std::uint64_t pair_id,
		std::uint32_t owner_thread) noexcept;
	[[nodiscard]] bool begin_eye(std::uint64_t pair_id,
		std::uint32_t eye) noexcept;
	void observe_backend_view_copy(std::uint64_t pair_id, std::uint32_t eye,
		std::uint32_t ordinal, const void* owner_record,
		const void* backend_state) noexcept;
	// Wrap H2's 0x1407A2E60 primary scene-batch call. pair16 names the
	// caller-owned 16-byte backend pair copied by that leaf. All arguments are
	// observed only; neither begin nor end retains ownership of engine memory.
	[[nodiscard]] bool begin_primary(std::uint64_t pair_id, std::uint32_t eye,
		const void* scene_context, const void* pair16,
		const void* source_descriptor, std::uint32_t mode,
		std::uintptr_t return_address, primary_token& output) noexcept;
	[[nodiscard]] bool end_primary(primary_token& token,
		const void* scene_context, const void* pair16,
		const void* source_descriptor) noexcept;
	// Wrap H2's local executors (0x1407A2B70 complex and 0x1407A2A90
	// simple). The fixed-size parser discovers the registered consumer
	// functions itself and records their source shapes and cursor movement.
	[[nodiscard]] bool begin_executor(std::uint64_t pair_id, std::uint32_t eye,
		local_executor_kind kind, const void* header, const void* local_state,
		std::uintptr_t return_address, executor_token& output) noexcept;
	[[nodiscard]] bool end_executor(executor_token& token, const void* header,
		const void* local_state) noexcept;
	// Observe the exact admission call made by one of the four selected H2
	// consumers. The wrapper calls H2 first and reports its unmodified result.
	void observe_consumer_gate(std::uint64_t pair_id, std::uint32_t eye,
		local_consumer_kind consumer, const void* context, std::uint64_t key,
		bool accepted, std::uintptr_t return_address) noexcept;
	// Reaching H2's 0x1407A4950 call proves that the consumer took its internal
	// second render pass. This observer never replaces or suppresses that call.
	void observe_consumer_second_pass(std::uint64_t pair_id, std::uint32_t eye,
		local_consumer_kind consumer, const void* context,
		std::uintptr_t return_address) noexcept;
	void note_hooks(bool installed, std::uint64_t install_attempts,
		std::uint64_t install_failures,
		const std::array<std::uintptr_t, local_hook_count>& targets) noexcept;
	[[nodiscard]] bool end_eye(std::uint64_t pair_id,
		std::uint32_t eye) noexcept;
	void end_pair(std::uint64_t pair_id, bool owner_complete) noexcept;
	void get_report(report& output) noexcept;
	[[nodiscard]] const char* to_string(state value) noexcept;
	[[nodiscard]] const char* to_string(mismatch_kind value) noexcept;
	[[nodiscard]] const char* to_string(local_executor_kind value) noexcept;
	[[nodiscard]] const char* to_string(local_consumer_kind value) noexcept;
	[[nodiscard]] const char* to_string(observer_stage value) noexcept;
}
