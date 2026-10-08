#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "component/d3d11.hpp"
#include "frame_capture.hpp"
#include "steamvr_input_diagnostics.hpp"

namespace vr
{
	enum class scene_mode
	{
		synthetic,
		backbuffer,
		engine_stereo,
	};

	enum class runtime_state
	{
		disabled,
		waiting_for_graphics,
		sdk_headers_unavailable,
		loader_missing,
		runtime_unavailable,
		no_hmd,
		graphics_mismatch,
		session_idle,
		running,
		recoverable_error,
		fatal_for_vr,
	};

	struct eye_status
	{
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint64_t acquired{};
		std::uint64_t released{};
	};

	struct submit_format_candidate_status
	{
		std::uint32_t format{};
		std::int64_t check_format_result{-1};
		std::uint32_t format_support{};
		bool ring_ready{};
		std::string creation_error{};
		std::int64_t left_submit_result{-1};
		std::int64_t right_submit_result{-1};
		std::uint32_t accepted_eye_mask{};
		bool retired{};
	};

	struct submit_format_probe_status
	{
		std::string state{"unarmed"};
		bool active{};
		bool complete{};
		std::uint32_t current_candidate{};
		std::int64_t selected_format{};
		std::uint64_t candidate_attempts{};
		std::uint64_t candidate_rejections{};
		std::uint64_t clear_last_frame_calls{};
		std::array<submit_format_candidate_status, 3> candidates{};
	};

	struct runtime_status
	{
		runtime_state state{runtime_state::disabled};
		bool sdk_headers_available{};
		bool desired_enabled{};
		bool applied_enabled{};
		bool reinitialize_pending{};
		bool implicit_layer_policy_applied{};
		bool runtime_override_active{};
		bool runtime_override_set_by_policy{};
		bool loader_loaded{};
		bool instance_created{};
		bool session_created{};
		bool session_running{};
		std::uint64_t device_generation{};
		std::uint64_t initialization_attempt_count{};
		std::uint64_t cleanup_count{};
		std::uint32_t implicit_layer_manifest_count{};
		std::uint32_t implicit_layers_disabled{};
		std::uint64_t session_generation{};
		std::uint64_t session_ready_count{};
		std::uint64_t session_stopping_count{};
		std::uint64_t session_begin_count{};
		std::uint64_t session_end_count{};
		std::uint32_t view_count{};
		std::int64_t color_format{};
		std::uint32_t recommended_eye_width{};
		std::uint32_t recommended_eye_height{};
		std::int32_t session_state{};
		std::int64_t last_xr_result{};
		std::uint64_t submitted_frames{};
		// A frame context is sampled on the renderer thread before the engine
		// builds its view-dependent scene. Submission may happen later, but the
		// eye family and pose must come from this same context.
		std::uint64_t frame_context_id{};
		std::uint64_t frame_context_prepare_count{};
		std::uint64_t frame_context_miss_count{};
		std::uint64_t tracking_pose_sample_count{};
		std::uint64_t tracking_pose_failure_count{};
		std::uint64_t tracking_last_present_frame{};
		std::string frame_phase;
		bool present_handoff_pending{};
		std::uint64_t completed_present_handoff_count{};
		std::uint64_t present_to_wait_get_poses_us{};
		std::uint64_t wait_get_poses_call_count{};
		std::uint64_t wait_get_poses_last_us{};
		std::uint64_t wait_get_poses_max_us{};
		std::uint64_t wait_get_poses_total_us{};
		std::uint64_t wait_get_poses_at_least_2ms_count{};
		std::uint64_t wait_get_poses_at_least_11ms_count{};
		std::uint64_t wait_get_poses_at_least_50ms_count{};
		std::uint64_t wait_get_poses_at_least_100ms_count{};
		std::uint64_t prepared_pair_ready_sample_count{};
		std::uint64_t prepared_pair_pending_presents_last{};
		std::uint64_t prepared_pair_pending_presents_max{};
		std::uint64_t prepared_pair_pending_presents_total{};
		std::uint64_t prepared_pair_ready_last_us{};
		std::uint64_t prepared_pair_ready_max_us{};
		std::uint64_t prepared_pair_ready_total_us{};
		bool present_owner_transaction_active{};
		std::uint64_t present_owner_transaction_frame{};
		std::uint64_t present_owner_transaction_generation{};
		std::uint32_t present_owner_transaction_thread_id{};
		std::uint64_t present_owner_pre_count{};
		std::uint64_t present_owner_post_count{};
		std::uint64_t present_owner_last_completed_frame{};
		std::int32_t present_owner_last_post_hresult{};
		bool submission_on_game_device{};
		std::uint64_t adapter_preflight_attempts{};
		std::uint64_t adapter_preflight_successes{};
		std::uint64_t adapter_preflight_failures{};
		std::uint64_t direct_pairs_acquired{};
		std::uint64_t direct_pairs_retired{};
		std::uint64_t direct_pairs_deferred{};
		std::uint64_t direct_submit_attempts{};
		std::uint64_t direct_submit_failures{};
		std::uint64_t submit_call_count{};
		std::uint64_t submit_last_us{};
		std::uint64_t submit_max_us{};
		std::uint64_t submit_total_us{};
		std::uint64_t submit_to_retirement_count{};
		std::uint64_t submit_to_retirement_last_us{};
		std::uint64_t submit_to_retirement_max_us{};
		std::uint64_t submit_to_retirement_total_us{};
		std::uint64_t compositor_frame_timing_query_count{};
		bool compositor_frame_timing_valid{};
		std::uint32_t compositor_frame_timing_frame_index{};
		std::uint32_t compositor_frame_timing_dropped_frames{};
		std::uint32_t compositor_frame_timing_mispresented{};
		float compositor_frame_timing_client_interval_ms{};
		float compositor_frame_timing_present_cpu_ms{};
		float compositor_frame_timing_wait_for_present_cpu_ms{};
		float compositor_frame_timing_submit_frame_ms{};
		float compositor_frame_timing_pre_submit_gpu_ms{};
		float compositor_frame_timing_post_submit_gpu_ms{};
		float compositor_frame_timing_total_render_gpu_ms{};
		submit_format_probe_status submit_format_probe{};
		std::uint32_t direct_renderer_thread_id{};
		std::uint32_t direct_present_owner_thread_id{};
		std::uint32_t direct_scene_owner_thread_id{};
		bool direct_present_owner_contract_valid{};
		std::uint64_t direct_present_owner_contract_violations{};
		std::string graphics_transport;
		scene_mode requested_scene_mode{scene_mode::engine_stereo};
		scene_mode effective_scene_mode{scene_mode::engine_stereo};
		std::uint64_t compositor_prepare_count{};
		std::uint64_t compositor_render_count{};
		std::uint64_t compositor_source_miss_count{};
		frame_capture_status capture;
		std::string last_compositor_error;
		std::string menu_surface_mode;
		std::string backend_name;
		std::string backend_selection_reason;
		std::string last_initialization_stage;
		std::string disabled_implicit_layers;
		std::string implicit_layer_policy_warning;
		std::string runtime_override_manifest;
		std::string runtime_override_source;
		std::string runtime_selection_diagnostic;
		std::string runtime_name;
		bool application_registered{};
		std::string application_registration_error;
		bool controller_input_ready{};
		std::string controller_pose_reference;
		std::array<std::string, 2> controller_reference_ids;
		std::string controller_pose_reference_error;
		std::string controller_input_error;
		steamvr_input::diagnostic_snapshot openvr_input_diagnostics;
		std::string runtime_manifest;
		std::string runtime_library;
		std::string system_name;
		std::string last_runtime_name;
		std::string last_system_name;
		std::string session_state_name;
		std::string last_xr_result_name;
		std::string blend_mode;
		std::string last_error;
		// Runtime/session readiness is independent from the H2 native renderer.
		// A renderer target failure must not tear down an otherwise valid SteamVR
		// session; it is reported here until the renderer path is repaired.
		bool native_renderer_ready{};
		std::uint64_t native_renderer_failure_count{};
		std::string native_renderer_error;
		std::array<eye_status, 2> eyes{};
		bool worker_active{};
		std::uint64_t worker_wakeup_count{};
		std::uint64_t worker_present_count{};
		std::uint64_t worker_dropped_present_count{};
		std::uint64_t worker_backend_call_count{};
		bool worker_graphics_available{};
		std::string worker_phase;
		std::string worker_last_error;
		std::uint64_t worker_last_present_sequence{};
		std::uint64_t worker_last_generation{};
		std::uint64_t worker_last_configuration_generation{};
		std::uint64_t worker_last_status_update{};
		std::uint64_t worker_progress_age_ms{};
		std::uint64_t worker_watchdog_count{};
		bool worker_progress_stalled{};
		std::string worker_current_operation;
		std::string worker_last_completed_operation;
	};

	class runtime final
	{
	  public:
		static runtime& get();

		void set_desired_enabled(bool enabled);
		void set_scene_mode(scene_mode mode);
		void request_reinitialize();
		void prepare_frame(const d3d11::device_snapshot& graphics, std::uint64_t frame_index);
		[[nodiscard]] bool initialize(const d3d11::device_snapshot& graphics);
		void on_present(const d3d11::device_snapshot& graphics, std::uint64_t frame_index);
		void on_present(const d3d11::present_event& event);
		void on_present_post(const d3d11::present_event& event, HRESULT result);
		void capture_present(const d3d11::present_event& event);
		[[nodiscard]] bool capture_engine_texture(const d3d11::device_snapshot& graphics,
		                                          ID3D11Texture2D* source,
		                                          capture_frame_tag tag);
		void poll_capture(const d3d11::device_snapshot& graphics);
		void on_resize_before(const d3d11::resize_event& event) noexcept;
		void on_device_destroying(const d3d11::device_snapshot& graphics) noexcept;
		void shutdown() noexcept;
		[[nodiscard]] bool shutdown_complete() noexcept;

		[[nodiscard]] bool requested_enabled() const;
		[[nodiscard]] bool applied_enabled() const;
		[[nodiscard]] runtime_status get_status() const;

	  private:
		runtime();
		~runtime();
		runtime(const runtime&) = delete;
		runtime& operator=(const runtime&) = delete;

		class implementation;
		std::unique_ptr<implementation> implementation_;
	};

	[[nodiscard]] const char* to_string(runtime_state state) noexcept;
}
