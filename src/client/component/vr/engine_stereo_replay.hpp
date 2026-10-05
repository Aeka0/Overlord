#pragma once

#include "component/d3d11.hpp"
#include "engine_command_stream.hpp"
#include "engine_stereo_binding.hpp"
#include "engine_stereo_draw_indexed.hpp"

#include <array>
#include <cstdint>

namespace vr::engine_stereo_replay
{
	enum class gate_state : std::uint8_t
	{
		waiting,
		ready,
		active,
		readback_pending,
		complete,
		failed,
	};

	enum class failure : std::uint8_t
	{
		none,
		prerequisite,
		scene_batch_selection,
		device,
		target_description,
		resource_creation,
		transaction,
		view_copy,
		output_binding,
		command_stream,
		draw_replay,
		readback,
		content,
		present,
		timeout,
	};

	struct report
	{
		std::uint64_t publication_sequence{};
		std::uintptr_t record{};
		std::uintptr_t context{};
		std::uint64_t device_generation{};
		std::uint32_t owner_thread_id{};
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t format{};
		std::uint32_t natural_copy_calls{};
		std::uint32_t natural_left_substitutions{};
		std::uint32_t replay_view_substitutions{};
		std::uint32_t replay_dispatches{};
		std::uint32_t natural_draw_calls{};
		std::uint32_t replay_draw_calls{};
		std::uint32_t replay_output_bind_calls{};
		std::uint64_t natural_draw_hash{};
		std::uint64_t replay_draw_hash{};
		engine_stereo_draw_indexed::replay_counter natural_draw_trace{};
		engine_stereo_draw_indexed::replay_counter replay_draw_trace{};
		engine_stereo_draw_indexed::replay_comparison draw_comparison{};
		engine_command_stream::snapshot command_before{};
		engine_command_stream::snapshot command_after{};
		std::uint64_t left_hash{};
		std::uint64_t clear_hash{};
		std::uint64_t right_hash{};
		std::uint64_t left_nonzero_bytes{};
		std::uint64_t clear_nonzero_bytes{};
		std::uint64_t right_nonzero_bytes{};
		std::uint64_t started_present{};
		std::uint64_t completed_present{};
		std::uint32_t readback_polls{};
		HRESULT swap_chain_buffer_result{E_PENDING};
		HRESULT target_result{E_PENDING};
		HRESULT target_view_result{E_PENDING};
		HRESULT left_staging_result{E_PENDING};
		HRESULT clear_staging_result{E_PENDING};
		HRESULT right_staging_result{E_PENDING};
		HRESULT query_result{E_PENDING};
		HRESULT query_poll_result{E_PENDING};
		HRESULT left_map_result{E_PENDING};
		HRESULT clear_map_result{E_PENDING};
		HRESULT right_map_result{E_PENDING};
		bool output_restored{};
		bool view_restored{};
		bool command_immutable{};
		bool replay_draw_matches{};
		bool right_changed_from_clear{};
		bool eyes_distinct{};
		failure error{failure::none};
	};

	struct transaction
	{
		bool active{};
		bool failed{};
		std::uint64_t publication_sequence{};
		std::uintptr_t record{};
		std::uintptr_t backend_state{};
		std::uint32_t owner_thread_id{};
		std::uint32_t natural_copy_calls{};
		std::uint32_t natural_left_substitutions{};
		std::uint32_t replay_view_substitutions{};
		std::uint32_t replay_dispatches{};
		bool natural_draw_complete{};
		engine_stereo_draw_indexed::replay_counter natural_draw{};
		failure error{failure::none};
		alignas(16) std::array<std::uint8_t,
			engine_stereo_view::h2_view_slot_size> left{};
		alignas(16) std::array<std::uint8_t,
			engine_stereo_view::h2_view_slot_size> right{};
		void (*copy_original)(void*){};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return active;
		}
	};

	struct status
	{
		gate_state state{gate_state::waiting};
		failure error{failure::none};
		std::uint64_t preparations{};
		std::uint64_t attempts{};
		std::uint64_t completions{};
		std::uint64_t failures{};
		std::uint64_t replay_dispatches{};
		std::uint64_t replay_draw_calls{};
		std::uint64_t readback_polls{};
		std::uint64_t latest_publication_sequence{};
		std::uintptr_t latest_record{};
		std::uint64_t started_present{};
		std::uint64_t completed_present{};
	};

	using h2_copy_fn = void(*)(void* backend_state);
	using h2_dispatch_fn = void(*)(void* commands, const int* filter,
		bool flagged_mode);

	void on_present_pre(const d3d11::present_event& event) noexcept;
	void on_present_post(const d3d11::present_event& event,
		HRESULT result) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t generation) noexcept;

	[[nodiscard]] bool begin(transaction& output,
		const engine_stereo_binding::backend_claim& claim,
		std::uintptr_t record) noexcept;
	void invoke_natural_copy(transaction& active, void* backend_state,
		h2_copy_fn original) noexcept;
	void enter_natural_dispatch(transaction& active) noexcept;
	void leave_natural_dispatch(transaction& active) noexcept;
	void execute(transaction& active, void* commands, const int* filter,
		bool flagged_mode, h2_dispatch_fn original) noexcept;
	void end(transaction& active, bool natural_dispatch_returned) noexcept;

	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] bool read_report(report& output) noexcept;
	[[nodiscard]] const char* to_string(gate_state state) noexcept;
	[[nodiscard]] const char* to_string(failure error) noexcept;
	[[nodiscard]] bool reset() noexcept;
}
