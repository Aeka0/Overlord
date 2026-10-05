#pragma once

#include "engine_stereo_probe.hpp"

#include <cstdint>
#include <chrono>
#include <string>

namespace d3d11
{
	struct graphics_status;
	struct present_event;
	struct resize_event;
}

namespace vr::engine_backend_probe
{
	struct backend_watchdog_status;
}

namespace vr::diagnostics
{
	enum class trace_event : std::uint16_t
	{
		renderer_frame = 1,
		present_pre,
		present_post,
		resize_before,
		runtime_prepare_frame,
		runtime_prepare_frame_result,
		runtime_wait_get_poses,
		runtime_wait_get_poses_result,
		runtime_direct_submission_frame,
		runtime_direct_thread_contract,
		runtime_prepare_scene,
		runtime_prepare_scene_result,
		runtime_source_pair,
		runtime_source_pair_result,
		runtime_render_eye,
		runtime_render_eye_result,
		runtime_submit,
		runtime_submit_result,
		runtime_interop_copy,
		runtime_interop_copy_result,
		runtime_interop_copy_eye,
		runtime_interop_copy_eye_result,
		runtime_interop_copy_query_end,
		runtime_interop_copy_query_end_result,
		runtime_interop_copy_query_poll,
		runtime_interop_copy_query_poll_result,
		runtime_submission_retire,
		runtime_post_present_handoff,
		runtime_post_present_handoff_result,
		scene_hook_enter,
		scene_native_gate,
		scene_native_target,
		scene_native_call,
		scene_native_return,
		scene_original_call,
		native_session_ensure,
		native_session_ensure_result,
		native_capture,
		native_capture_result,
		capture_produce,
		capture_direct,
		capture_copy,
		capture_copy_begin,
		capture_copy_end,
		capture_query_end,
		capture_query_end_begin,
		capture_query_end_end,
		capture_poll,
		capture_poll_begin,
		capture_poll_end,
		capture_open_shared,
		capture_retain_direct,
		scene_source_ensure_begin,
		scene_source_ensure_result,
		scene_gpu_copy_begin,
		scene_gpu_copy_end,
		scene_gpu_fence,
		scene_gpu_fence_result,
		native_conversion_begin,
		native_conversion_end,
		native_command_list_begin,
		native_command_list_end,
		native_pair_release,
		runtime_state_change,
		runtime_event,
		runtime_teardown_begin,
		runtime_teardown_stage,
		dynamic_upload_defer,
		dynamic_upload_advance,
		dynamic_upload_return,
		dynamic_upload_recover,
		dynamic_upload_reject,
		vehicle_aim_prepare,
		vehicle_shot,
		vehicle_fx,
	};

	// Fixed-size and lock-free: safe to call from the renderer hook and readable
	// by the crash handler even if a VR/status mutex is held by the faulting thread.
	void record_trace(trace_event event, std::uint64_t value_a = 0,
		std::uint64_t value_b = 0) noexcept;
	[[nodiscard]] std::string crash_trace();

	void record_renderer_frame() noexcept;
	void record_gui_frame() noexcept;
	void record_present_pre(const d3d11::present_event& event) noexcept;
	void record_present_post(const d3d11::present_event& event, std::int32_t result) noexcept;
	void record_resize_before(const d3d11::resize_event& event) noexcept;
	void scene_hook_entered() noexcept;
	void scene_hook_exited() noexcept;

	struct scene_hook_watchdog_status
	{
		std::uint64_t sequence{};
		std::uint64_t entered_tick{};
		std::uint32_t thread_id{};
		std::uint32_t depth{};
	};

	[[nodiscard]] scene_hook_watchdog_status get_scene_hook_watchdog_status() noexcept;
	void gpu_interop_entered(std::uintptr_t source, std::uintptr_t destination) noexcept;
	void gpu_interop_exited() noexcept;

	struct gpu_interop_watchdog_status
	{
		std::uint64_t sequence{};
		std::uint64_t entered_tick{};
		std::uint32_t thread_id{};
		std::uint32_t depth{};
		std::uintptr_t source{};
		std::uintptr_t destination{};
	};

	[[nodiscard]] gpu_interop_watchdog_status get_gpu_interop_watchdog_status() noexcept;

	void print_status(bool dvar_enabled);
	[[nodiscard]] bool write_status_snapshot(bool dvar_enabled) noexcept;
	bool write_soft_freeze_report(const d3d11::graphics_status& graphics,
		std::chrono::milliseconds age);
	bool write_scene_hook_stall_report(const d3d11::graphics_status& graphics,
		const scene_hook_watchdog_status& scene, std::chrono::milliseconds age);
	bool write_gpu_interop_stall_report(const d3d11::graphics_status& graphics,
		const gpu_interop_watchdog_status& interop, std::chrono::milliseconds age);
	bool write_backend_stall_report(const d3d11::graphics_status& graphics,
		const engine_backend_probe::backend_watchdog_status& backend,
		std::chrono::milliseconds age);
	// Returns true only when this invocation atomically persisted the current
	// live trace. Terminal execution artifacts are checkpointed independently so
	// this lightweight snapshot can continue after the one-shot bundle completes.
	[[nodiscard]] bool write_live_trace_snapshot(
		const d3d11::graphics_status& graphics) noexcept;
	void print_engine_probe(const engine_stereo_probe::status& probe_status);
}
