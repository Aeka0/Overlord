#pragma once

#include "engine_stereo_binding.hpp"

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_output_merger
{
	using clear_render_target_observer_fn = void(*)(ID3D11DeviceContext*,
		ID3D11RenderTargetView*, std::uintptr_t, std::uint32_t, std::uintptr_t,
		std::uint64_t) noexcept;
	using clear_depth_stencil_observer_fn = void(*)(ID3D11DeviceContext*,
		ID3D11DepthStencilView*, std::uint32_t, std::uintptr_t, std::uint32_t,
		std::uintptr_t, std::uint64_t) noexcept;
	using clear_state_observer_fn = void(*)(ID3D11DeviceContext*,
		std::uintptr_t) noexcept;
	enum class clear_observer_channel : std::uint8_t
	{
		gpu_census,
		ssr_consumer,
		effect_timeline,
		count,
	};
	inline constexpr std::size_t clear_observer_channel_count =
		static_cast<std::size_t>(clear_observer_channel::count);
	using render_target_rewriter_fn = ID3D11RenderTargetView*(*)(
		ID3D11DeviceContext*, ID3D11RenderTargetView*) noexcept;
	using depth_stencil_rewriter_fn = ID3D11DepthStencilView*(*)(
		ID3D11DeviceContext*, ID3D11DepthStencilView*) noexcept;
	using unordered_access_rewriter_fn = ID3D11UnorderedAccessView*(*)(
		ID3D11DeviceContext*, ID3D11UnorderedAccessView*) noexcept;
	inline constexpr std::size_t maximum_events = 256;
	inline constexpr std::size_t maximum_render_targets =
		D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

	enum class gate_state : std::uint8_t
	{
		armed,
		active,
		complete,
		failed,
	};

	enum class phase : std::uint8_t
	{
		before_dispatch,
		dispatch,
		after_dispatch,
	};

	struct view_observation
	{
		std::uintptr_t view{};
		std::uintptr_t resource{};
		DXGI_FORMAT view_format{DXGI_FORMAT_UNKNOWN};
		std::uint32_t view_dimension{};
		D3D11_RESOURCE_DIMENSION resource_dimension{D3D11_RESOURCE_DIMENSION_UNKNOWN};
		UINT width{};
		UINT height{};
		UINT mip_levels{};
		UINT array_size{};
		DXGI_FORMAT resource_format{DXGI_FORMAT_UNKNOWN};
		UINT sample_count{};
		UINT sample_quality{};
		D3D11_USAGE usage{D3D11_USAGE_DEFAULT};
		UINT bind_flags{};
		UINT cpu_access_flags{};
		UINT misc_flags{};
		bool valid{};
	};

	struct event
	{
		std::uint64_t sequence{};
		std::uint64_t timestamp_qpc{};
		std::uint64_t publication_sequence{};
		std::uintptr_t record{};
		std::uintptr_t context{};
		std::uintptr_t caller{};
		std::uint32_t thread_id{};
		std::uint32_t render_target_count{};
		std::uint32_t latest_target_id{0xFFFFFFFFu};
		std::uint32_t view_copy_ordinal{};
		std::uint32_t view_substitution_ordinal{};
		std::uint32_t selected_eye{0xFFFFFFFFu};
		phase execution_phase{phase::before_dispatch};
		bool expected_context{};
		std::array<view_observation, maximum_render_targets> render_targets{};
		view_observation depth_stencil{};
	};

	struct report
	{
		std::uint64_t publication_sequence{};
		std::uintptr_t record{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint32_t transaction_thread_id{};
		std::uint32_t event_count{};
		std::array<event, maximum_events> events{};
	};

	struct transaction
	{
		bool active{};
		bool failed{};
		bool dispatch_active{};
		bool dispatch_entered{};
		bool dispatch_returned{};
		std::uint32_t latest_target_id{0xFFFFFFFFu};
		std::uint32_t view_copy_ordinal{};
		std::uint32_t view_substitution_ordinal{};
		std::uint32_t selected_eye{0xFFFFFFFFu};
		std::uint32_t bind_calls{};
		std::uint32_t nonnull_bind_calls{};
		std::uint32_t context_mismatches{};
		std::uint32_t thread_mismatches{};
		std::uint32_t query_failures{};
		std::uint32_t overflows{};
		std::uint64_t metadata_queries{};
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
		bool extended_hooks_installed{};
		std::uintptr_t hook_target{};
		std::uintptr_t unordered_access_hook_target{};
		std::uintptr_t clear_state_hook_target{};
		std::uintptr_t clear_render_target_hook_target{};
		std::uintptr_t clear_depth_stencil_hook_target{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint64_t hook_failures{};
		std::uint64_t attempts{};
		std::uint64_t completions{};
		std::uint64_t failures{};
		std::uint64_t bind_calls{};
		std::uint64_t nonnull_bind_calls{};
		std::uint64_t context_mismatches{};
		std::uint64_t thread_mismatches{};
		std::uint64_t query_failures{};
		std::uint64_t overflows{};
		std::uint64_t metadata_queries{};
		std::uint64_t unordered_access_calls{};
		std::uint64_t clear_state_calls{};
		std::uint64_t clear_render_target_calls{};
		std::uint64_t clear_depth_stencil_calls{};
		std::uint64_t binding_invalidations{};
		std::uint64_t latest_publication_sequence{};
		std::uintptr_t latest_record{};
	};

	struct replay_guard
	{
		bool active{};
		std::uintptr_t expected_context{};
		std::uint32_t thread_id{};
		std::uint32_t output_bind_calls{};
		std::uint32_t invalid_calls{};
	};

	struct binding_snapshot
	{
		bool valid{};
		std::uint64_t sequence{};
		std::uintptr_t context{};
		std::uint32_t render_target_count{};
		std::uint32_t target_id{0xFFFFFFFFu};
		std::uintptr_t render_target_0{};
		std::uintptr_t depth_stencil{};
	};

	// Install one process-wide detour at the active immediate-context method.
	// Calls outside the exact stereo backend transaction pass through untouched.
	[[nodiscard]] bool install(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;

	[[nodiscard]] bool begin(transaction& output,
		const engine_stereo_binding::backend_claim& claim,
		std::uintptr_t record) noexcept;
	void note_view_copy(transaction& active, std::uint32_t copy_ordinal,
		std::uint32_t substitution_ordinal, std::uint32_t selected_eye) noexcept;
	void note_target(transaction& active, std::uint32_t target_id) noexcept;
	void enter_dispatch(transaction& active) noexcept;
	void leave_dispatch(transaction& active) noexcept;
	void end(transaction& active, bool command_dispatch_returned) noexcept;

	// Prove that the accepted command dispatcher does not alter the output-merger
	// binding during one explicit replay. Any call is a hard contract failure;
	// this guard observes only and never substitutes a target.
	[[nodiscard]] bool begin_replay_guard(replay_guard& output,
		ID3D11DeviceContext* context) noexcept;
	[[nodiscard]] bool end_replay_guard(replay_guard& active) noexcept;
	[[nodiscard]] binding_snapshot get_current_binding(
		ID3D11DeviceContext* context) noexcept;
	// Optional read-only observers are registered by a bounded diagnostic owner.
	// Output-merger hooks remain independent and pass through when none is present.
	void set_clear_observers(clear_observer_channel channel,
		clear_render_target_observer_fn render_target,
		clear_depth_stencil_observer_fn depth_stencil) noexcept;
	// ClearState is already detoured here. Reuse that exact call instead of
	// stacking another hook when a bounded state-provenance probe is active.
	void set_clear_state_observer(clear_state_observer_fn observer) noexcept;
	// Optional exact-view redirection owned by the native stereo resource
	// isolator. The callbacks must return a borrowed view and remain strict
	// pass-through outside their active owner/eye transaction.
	void set_view_rewriters(render_target_rewriter_fn render_target,
		depth_stencil_rewriter_fn depth_stencil,
		unordered_access_rewriter_fn unordered_access) noexcept;
	// ExecuteCommandList(FALSE) replaces immediate-context state without
	// necessarily re-entering the public OMSetRenderTargets vtable method. Mark
	// the TLS snapshot unknown so later execution evidence cannot reuse a stale
	// RTV/DSV identity.
	void invalidate_current_binding(ID3D11DeviceContext* context) noexcept;

	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] bool read_report(report& output) noexcept;
	[[nodiscard]] const char* to_string(gate_state state) noexcept;
	[[nodiscard]] const char* to_string(phase value) noexcept;
	// Control-plane/test only. Refuses to reset an executing transaction.
	[[nodiscard]] bool reset() noexcept;
}
