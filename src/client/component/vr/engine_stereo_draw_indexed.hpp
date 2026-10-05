#pragma once

#include "engine_command_stream.hpp"
#include "engine_stereo_binding.hpp"

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_draw_indexed
{
	using draw_observer = void(*)(ID3D11DeviceContext*, std::uintptr_t,
		std::uint32_t) noexcept;
	using draw_original = void(__stdcall*)(ID3D11DeviceContext*, UINT, UINT, INT);
	using draw_copy_observer = void(*)(ID3D11DeviceContext*, UINT, UINT, INT, draw_original) noexcept;
	inline constexpr std::size_t maximum_events = 16384;
	inline constexpr std::size_t maximum_replay_events = 256;
	inline constexpr std::size_t maximum_boundary_groups = 256;

	enum class gate_state : std::uint8_t
	{
		armed,
		active,
		complete,
		failed,
	};

	struct event
	{
		std::uint64_t sequence{};
		std::uint64_t timestamp_qpc{};
		std::uintptr_t context{};
		std::uintptr_t caller{};
		std::uint32_t thread_id{};
		std::uint32_t index_count{};
		std::uint32_t start_index_location{};
		std::int32_t base_vertex_location{};
		bool expected_context{};
		bool arguments_valid{};
	};

	enum class boundary_phase : std::uint8_t
	{
		before_dispatch,
		dispatch,
		after_dispatch,
	};

	struct boundary_group
	{
		boundary_phase execution_phase{boundary_phase::before_dispatch};
		std::uint64_t binding_sequence{};
		std::uintptr_t render_target{};
		std::uintptr_t depth_stencil{};
		std::uint32_t render_target_count{};
		std::uint32_t target_id{0xFFFFFFFFu};
		std::uint32_t draw_calls{};
		std::uint64_t index_count{};
		std::uint64_t draw_call_hash{};
		std::uintptr_t first_caller{};
		std::uintptr_t last_caller{};
		std::uint32_t first_start_index{};
		std::uint32_t last_start_index{};
		std::int32_t first_base_vertex{};
		std::int32_t last_base_vertex{};
	};

	struct report
	{
		std::uint64_t publication_sequence{};
		std::uintptr_t record{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint32_t transaction_thread_id{};
		engine_command_stream::snapshot before{};
		engine_command_stream::snapshot after{};
		std::uint64_t draw_call_hash{};
		std::uint32_t event_count{};
		std::array<event, maximum_events> events{};
		std::uint32_t boundary_draw_calls{};
		std::uint32_t boundary_group_count{};
		std::uint32_t boundary_group_overflows{};
		std::array<boundary_group, maximum_boundary_groups> boundary_groups{};
	};

	struct transaction
	{
		bool active{};
		bool failed{};
		bool dispatch_active{};
		bool dispatch_entered{};
		bool dispatch_returned{};
		std::uint32_t draw_calls{};
		std::uint32_t context_mismatches{};
		std::uint32_t thread_mismatches{};
		std::uint32_t invalid_arguments{};
		std::uint32_t overflows{};
		report evidence{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return active;
		}
	};

	struct status
	{
		gate_state state{gate_state::armed};
		bool hook_installed{};
		std::uintptr_t hook_target{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint64_t hook_failures{};
		std::uint64_t attempts{};
		std::uint64_t completions{};
		std::uint64_t failures{};
		std::uint64_t draw_calls{};
		std::uint64_t context_mismatches{};
		std::uint64_t thread_mismatches{};
		std::uint64_t invalid_arguments{};
		std::uint64_t overflows{};
		std::uint64_t latest_publication_sequence{};
		std::uintptr_t latest_record{};
	};

	struct replay_counter
	{
		bool active{};
		std::uintptr_t expected_context{};
		std::uint32_t thread_id{};
		std::uint32_t draw_calls{};
		std::uint32_t context_mismatches{};
		std::uint32_t thread_mismatches{};
		std::uint32_t invalid_arguments{};
		std::uint32_t overflows{};
		std::uint64_t draw_call_hash{};
		std::uint64_t draw_shape_hash{};
		std::uint32_t event_count{};
		struct replay_event
		{
			std::uintptr_t caller{};
			std::uint32_t index_count{};
			std::uint32_t start_index_location{};
			std::int32_t base_vertex_location{};
		};
		std::array<replay_event, maximum_replay_events> events{};
	};

	struct replay_comparison
	{
		bool trace_complete{};
		bool shape_matches{};
		bool exact_matches{};
		bool dynamic_index_append_matches{};
		bool semantic_matches{};
		std::uint32_t start_index_delta{};
		std::uint64_t natural_index_count{};
	};

	[[nodiscard]] bool install(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	// One process-wide DrawIndexed detour owns the API boundary. Feature modules
	// attach a pass-through observer instead of installing or linking another hook.
	void set_draw_observer(draw_observer observer) noexcept;
	// Optional presentation copy after the original draw. The callback must restore
	// all modified context state and use the supplied trampoline, never re-enter DrawIndexed.
	void set_draw_copy_observer(draw_copy_observer observer) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;

	[[nodiscard]] bool begin(transaction& output,
		const engine_stereo_binding::backend_claim& claim,
		std::uintptr_t record) noexcept;
	void enter_dispatch(transaction& active, const void* commands) noexcept;
	void leave_dispatch(transaction& active, const void* commands) noexcept;
	void end(transaction& active, bool command_dispatch_returned) noexcept;

	// Count the exact DrawIndexed calls made by one explicitly bounded command
	// replay. This is independent of the one-shot natural observation report.
	[[nodiscard]] bool begin_replay(replay_counter& output,
		ID3D11DeviceContext* context) noexcept;
	[[nodiscard]] bool end_replay(replay_counter& active) noexcept;
	[[nodiscard]] replay_comparison compare_replay(
		const replay_counter& natural, const replay_counter& replay) noexcept;

	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] bool read_report(report& output) noexcept;
	[[nodiscard]] const char* to_string(gate_state state) noexcept;
	[[nodiscard]] const char* to_string(boundary_phase value) noexcept;
	[[nodiscard]] bool reset() noexcept;
}
