#pragma once

#include "engine_stereo_binding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_backend_target
{
	inline constexpr std::size_t context_state_pointer_offset = 0x08;
	inline constexpr std::size_t current_target_offset = 0x20D4;
	inline constexpr std::size_t target_entry_size = 0x40;
	inline constexpr std::uint32_t target_capacity = 304;
	inline constexpr std::size_t maximum_route_events = 256;
	inline constexpr std::size_t maximum_frame_route_events = 512;
	inline constexpr std::uint32_t invalid_target = 0xFFFFFFFFu;

	enum class gate_state : std::uint8_t
	{
		armed,
		active,
		closing,
		complete,
		failed,
	};

	enum class route_phase : std::uint8_t
	{
		outside_transaction,
		before_dispatch,
		dispatch,
		after_dispatch,
	};

	enum class frame_event_kind : std::uint8_t
	{
		target_select,
		view_copy,
	};

	struct route_event
	{
		std::uintptr_t context{};
		std::uintptr_t backend_state{};
		std::uintptr_t caller{};
		std::uint32_t target_id{invalid_target};
		std::uint32_t current_before{invalid_target};
		std::uint32_t current_after{invalid_target};
		route_phase phase{route_phase::before_dispatch};
		bool entry_stable{};
	};

	struct target_entry
	{
		bool seen{};
		std::array<std::uint8_t, target_entry_size> bytes{};
	};

	struct selection_scope
	{
		std::uint64_t backend_id{};
		std::uint64_t publication_sequence{};
		std::uintptr_t record{};
		std::uint32_t record_type{};
		bool backend_active{};
		bool binding_active{};
	};

	struct frame_route_event
	{
		std::uint64_t sequence{};
		std::uint64_t timestamp_qpc{};
		std::uint64_t latest_present_post_frame{};
		std::uint64_t backend_id{};
		std::uint64_t publication_sequence{};
		std::uintptr_t context{};
		std::uintptr_t backend_state{};
		std::uintptr_t caller{};
		std::uintptr_t record{};
		std::uint32_t thread_id{};
		std::uint32_t target_id{invalid_target};
		std::uint32_t current_before{invalid_target};
		std::uint32_t current_after{invalid_target};
		std::uint32_t record_type{};
		std::uint32_t view_copy_ordinal{};
		std::uint32_t view_substitution_ordinal{};
		std::uint32_t selected_eye{invalid_target};
		frame_event_kind kind{frame_event_kind::target_select};
		route_phase phase{route_phase::outside_transaction};
		bool backend_active{};
		bool binding_active{};
		bool view_source_matched{};
		bool entry_valid{};
		bool entry_stable{};
		bool application_matches{};
		std::array<std::uint8_t, target_entry_size> target_entry{};
	};

	struct frame_report
	{
		std::uint64_t start_publication_sequence{};
		std::uintptr_t start_record{};
		std::uint64_t start_present_post_frame{};
		std::uint32_t start_present_post_thread_id{};
		std::uint64_t end_present_pre_frame{};
		std::uint64_t device_generation{};
		std::uint32_t boundary_thread_id{};
		std::uint32_t event_count{};
		std::uint32_t view_copy_event_count{};
		std::uint32_t unique_target_count{};
		std::array<frame_route_event, maximum_frame_route_events> events{};
		std::array<target_entry, target_capacity> targets{};
	};

	struct report
	{
		std::uint64_t publication_sequence{};
		std::uintptr_t record{};
		std::uint32_t event_count{};
		std::uint32_t unique_target_count{};
		std::array<route_event, maximum_route_events> events{};
		std::array<target_entry, target_capacity> targets{};
	};

	struct transaction
	{
		bool active{};
		bool failed{};
		bool dispatch_active{};
		bool dispatch_entered{};
		bool dispatch_returned{};
		std::uint32_t select_calls{};
		std::uint32_t dispatch_select_calls{};
		std::uint32_t applied_transitions{};
		std::uint32_t retained_targets{};
		std::uint32_t invalid_observations{};
		std::uint32_t application_mismatches{};
		std::uint32_t entry_mutations{};
		std::uint32_t route_overflows{};
		report evidence{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return active;
		}
	};

	struct status
	{
		gate_state state{gate_state::armed};
		std::uint64_t attempts{};
		std::uint64_t completions{};
		std::uint64_t failures{};
		std::uint64_t select_calls{};
		std::uint64_t dispatch_select_calls{};
		std::uint64_t applied_transitions{};
		std::uint64_t retained_targets{};
		std::uint64_t invalid_observations{};
		std::uint64_t application_mismatches{};
		std::uint64_t entry_mutations{};
		std::uint64_t route_overflows{};
		std::uint64_t latest_publication_sequence{};
		std::uintptr_t latest_record{};
		std::uint32_t unique_targets{};
	};

	struct frame_status
	{
		gate_state state{gate_state::armed};
		std::uint64_t attempts{};
		std::uint64_t completions{};
		std::uint64_t failures{};
		std::uint64_t select_calls{};
		std::uint64_t invalid_observations{};
		std::uint64_t application_mismatches{};
		std::uint64_t entry_mutations{};
		std::uint64_t route_overflows{};
		std::uint64_t device_generation_mismatches{};
		std::uint64_t view_copy_events{};
		std::uint64_t start_publication_sequence{};
		std::uintptr_t start_record{};
		std::uint64_t start_present_post_frame{};
		std::uint32_t start_present_post_thread_id{};
		std::uint64_t end_present_pre_frame{};
		std::uint64_t device_generation{};
		std::uint32_t boundary_thread_id{};
		std::uint32_t active_writers{};
		std::uint32_t maximum_active_writers{};
		std::uint32_t unique_targets{};
	};

	using h2_target_select_fn = void(*)(void* context, std::uint32_t target_id);

	// Own exactly one claimed H2 backend transaction. This gate observes target
	// selection only; it never writes a registry entry or calls a D3D interface.
	[[nodiscard]] bool begin(transaction& output,
		const engine_stereo_binding::backend_claim& claim,
		std::uintptr_t record) noexcept;
	void enter_dispatch(transaction& active) noexcept;
	void leave_dispatch(transaction& active) noexcept;
	void invoke_select(transaction& active, void* context,
		std::uint32_t target_id, std::uintptr_t caller,
		const selection_scope& scope,
		const void* target_registry, std::uint32_t registry_capacity,
		h2_target_select_fn original) noexcept;
	// Merge an H2-native view-copy observation into the same claim-to-Present
	// timeline as target selection. This records CPU evidence only.
	void record_view_copy(const transaction& active, std::uintptr_t caller,
		const selection_scope& scope, std::uint32_t copy_ordinal,
		std::uint32_t substitution_ordinal, std::uint32_t selected_eye,
		bool source_matched) noexcept;
	void end(transaction& active, bool command_dispatch_returned) noexcept;
	// The frame observer starts with the first exact stereo backend claim and
	// closes at the following Present-pre boundary. These callbacks only publish
	// CPU evidence and never call D3D, OpenVR, or write the target registry.
	void on_present_pre(std::uint64_t frame_index, std::uint64_t device_generation,
		std::uint32_t thread_id) noexcept;
	void on_present_post(std::uint64_t frame_index, std::uint64_t device_generation,
		std::uint32_t thread_id, std::int32_t result) noexcept;

	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] bool read_report(report& output) noexcept;
	[[nodiscard]] frame_status get_frame_status() noexcept;
	[[nodiscard]] bool read_frame_report(frame_report& output) noexcept;
	[[nodiscard]] const char* to_string(gate_state state) noexcept;
	[[nodiscard]] const char* to_string(route_phase phase) noexcept;
	[[nodiscard]] const char* to_string(frame_event_kind kind) noexcept;
	// Control-plane/test only. Refuses to reset an executing backend transaction.
	[[nodiscard]] bool reset() noexcept;
}
