#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "input_status.hpp"
#include "steamvr_input_status.hpp"
#include "../controller_input.hpp"
#include "../desktop_mirror.hpp"
#include "../engine_scene_resolution.hpp"
#include "../head_pose_bridge.hpp"
#include "../vr_runtime.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr::diagnostics::detail
{
	namespace
	{
		const char* scene_mode_name(const scene_mode mode) noexcept
		{
			switch (mode)
			{
			case scene_mode::synthetic:
				return "synthetic";
			case scene_mode::backbuffer:
				return "backbuffer";
			case scene_mode::engine_stereo:
				return "engine_stereo";
			default:
				return "unknown";
			}
		}

	}

	void append_runtime_status(std::ostringstream& output,
		const vr::runtime_status& runtime_status,
		const head_pose_bridge::status& head_status)
	{
		output << "runtime:\n";
		output << "  backend=" << available(runtime_status.backend_name);
		output << " selection=" << available(runtime_status.backend_selection_reason) << '\n';
		output << "  menu_surface=" << available(runtime_status.menu_surface_mode) << '\n';
		output << "  sdk_headers_available=" << yes_no(runtime_status.sdk_headers_available);
		output << " state=" << to_string(runtime_status.state) << '\n';
		output << "  loader_loaded=" << yes_no(runtime_status.loader_loaded);
		output << " instance_created=" << yes_no(runtime_status.instance_created);
		output << " session_created=" << yes_no(runtime_status.session_created);
		output << " session_running=" << yes_no(runtime_status.session_running) << '\n';
		output << "  last_initialization_stage=" << available(runtime_status.last_initialization_stage);
		output << " initialization_attempts=" << runtime_status.initialization_attempt_count;
		output << " cleanups=" << runtime_status.cleanup_count << '\n';
		output << "  implicit_layer_policy: applied=" << yes_no(runtime_status.implicit_layer_policy_applied);
		output << " manifests=" << runtime_status.implicit_layer_manifest_count;
		output << " disabled=" << runtime_status.implicit_layers_disabled << '\n';
		output << "  implicit_layer_policy_disabled=" << available(runtime_status.disabled_implicit_layers) << '\n';
		output << "  implicit_layer_policy_warning=" << available(runtime_status.implicit_layer_policy_warning) << '\n';
		output << "  runtime_override: active=" << yes_no(runtime_status.runtime_override_active);
		output << " automatic=" << yes_no(runtime_status.runtime_override_set_by_policy);
		output << " source=" << available(runtime_status.runtime_override_source) << '\n';
		output << "  runtime_override_manifest=" << available(runtime_status.runtime_override_manifest) << '\n';
		output << "  runtime_selection=" << available(runtime_status.runtime_selection_diagnostic) << '\n';
		output << "  runtime_manifest=" << available(runtime_status.runtime_manifest) << '\n';
		output << "  runtime_library=" << available(runtime_status.runtime_library) << '\n';
		output << "  session_generation=" << runtime_status.session_generation;
		output << " READY=" << runtime_status.session_ready_count;
		output << " STOPPING=" << runtime_status.session_stopping_count;
		output << " begin=" << runtime_status.session_begin_count;
		output << " end=" << runtime_status.session_end_count << '\n';
		output << "  application_registered=" << yes_no(runtime_status.application_registered);
		output << " application_registration_error=" << available(runtime_status.application_registration_error) << '\n';
		output << "  runtime_name=" << available(runtime_status.runtime_name);
		output << " controller_input_ready=" << runtime_status.controller_input_ready;
		output << " controller_input_error=" << available(runtime_status.controller_input_error);
		output << "\n  controller_grip_reference=" << available(runtime_status.controller_pose_reference);
		output << " references=" << available(runtime_status.controller_reference_ids[0]) << '/'
			<< available(runtime_status.controller_reference_ids[1]);
		output << " reference_error=" << available(runtime_status.controller_pose_reference_error);
		const auto controls = controller_input::latest();
		output << "\n  controllers: pose_pipeline=" << controller_pose_pipeline::name(controls.pose_pipeline)
			<< " sequence=" << controls.sequence
			<< " focus=" << controls.focused << " move_active=" << controls.move_active
			<< " turn_active=" << controls.turn_active
			<< " sprint_active=" << controls.sprint.active << " sprint_down=" << controls.sprint.down
			<< " sprint_presses=" << controls.sprint.presses << " sprint_generation=" << controls.sprint.generation
			<< " jump_active=" << controls.jump.active << " jump_down=" << controls.jump.down
			<< " jump_presses=" << controls.jump.presses << " jump_generation=" << controls.jump.generation
			<< " trigger_left_active/down=" << controls.trigger[0].active << '/' << controls.trigger[0].down
			<< " trigger_right_active/down=" << controls.trigger[1].active << '/' << controls.trigger[1].down
			<< " squeeze_left_active/down=" << controls.squeeze[0].active << '/' << controls.squeeze[0].down
			<< " squeeze_right_active/down=" << controls.squeeze[1].active << '/' << controls.squeeze[1].down
			<< " move=" << controls.move[0] << ',' << controls.move[1]
			<< " turn=" << controls.turn[0] << ',' << controls.turn[1]
			<< " grip_valid=" << controls.grip[0].valid << ',' << controls.grip[1].valid
			<< " aim_valid=" << controls.aim[0].valid << ',' << controls.aim[1].valid;
		output << " system_name=" << available(runtime_status.system_name) << '\n';
		append_input_history(output,controller_input::get_input_history(),controller_input::clock::now());
		append_steamvr_input(output,runtime_status.openvr_input_diagnostics,controller_input::clock::now());
		output << "  last_runtime_name=" << available(runtime_status.last_runtime_name);
		output << " last_system_name=" << available(runtime_status.last_system_name) << '\n';
		output << "  session_state=" << runtime_status.session_state;
		output << " (" << available(runtime_status.session_state_name) << ')';
		output << " device_generation=" << runtime_status.device_generation << '\n';
		output << "  views=" << runtime_status.view_count;
		output << " format=" << runtime_status.color_format;
		output << " recommended=" << runtime_status.recommended_eye_width << 'x'
			<< runtime_status.recommended_eye_height;
		output << " blend_mode=" << available(runtime_status.blend_mode) << '\n';
		const auto resolution = engine_scene_resolution::get_report();
		output << "  native_resolution: state=" << engine_scene_resolution::to_string(resolution.state)
			<< " hooks=" << yes_no(resolution.hooks_installed)
			<< " requested=" << resolution.requested.width << 'x' << resolution.requested.height
			<< " scene=" << resolution.config.scene.width << 'x' << resolution.config.scene.height
			<< " scene_base=" << resolution.config.scene_base.width << 'x' << resolution.config.scene_base.height
			<< " color=" << resolution.color.width << 'x' << resolution.color.height
			<< " depth=" << resolution.depth.width << 'x' << resolution.depth.height
			<< " desktop=" << resolution.desktop.width << 'x' << resolution.desktop.height
			<< " samples=" << resolution.color_samples << '/' << resolution.depth_samples
			<< " rebuilds=" << resolution.rebuilds << " thread=" << resolution.apply_thread << '\n';
		output << "  native_resolution_error=" << available(resolution.error) << '\n';
		const auto mirror = desktop_mirror::get_report();
		output << "  desktop_mirror: mode=right_eye_symmetric_projection state=" << desktop_mirror::to_string(mirror.phase)
			<< " draws=" << mirror.draws << " last_pair=" << mirror.last_pair
			<< " last_result=" << mirror.last_result
			<< " requested_hfov=" << mirror.requested_fov << " effective_hfov=" << mirror.effective_fov
			<< " fov_limited=" << yes_no(mirror.fov_limited)
			<< " stabilization_strength=" << mirror.smoothing_strength << " correction_fraction=" << mirror.correction_fraction << '\n';
		output << "  frame_context: id=" << runtime_status.frame_context_id;
		output << " prepared=" << runtime_status.frame_context_prepare_count;
		output << " misses=" << runtime_status.frame_context_miss_count << '\n';
		output << "  tracking_pose: samples=" << runtime_status.tracking_pose_sample_count;
		output << " failures=" << runtime_status.tracking_pose_failure_count;
		output << " last_present=" << runtime_status.tracking_last_present_frame << '\n';
		output << "  frame_protocol: phase=" << available(runtime_status.frame_phase);
		output << " present_pending=" << yes_no(runtime_status.present_handoff_pending);
		output << " handoffs=" << runtime_status.completed_present_handoff_count;
		output << " present_to_wait_us=" << runtime_status.present_to_wait_get_poses_us << '\n';
		if (runtime_status.backend_name == "openvr")
		{
			const auto wait_average = runtime_status.wait_get_poses_call_count != 0
				? runtime_status.wait_get_poses_total_us /
					runtime_status.wait_get_poses_call_count
				: 0;
			output << "  openvr_wait_get_poses: calls="
				<< runtime_status.wait_get_poses_call_count;
			output << " us(last/max/avg)=" << runtime_status.wait_get_poses_last_us
				<< '/' << runtime_status.wait_get_poses_max_us << '/' << wait_average;
			output << " at_least_ms(2/11/50/100)="
				<< runtime_status.wait_get_poses_at_least_2ms_count << '/'
				<< runtime_status.wait_get_poses_at_least_11ms_count << '/'
				<< runtime_status.wait_get_poses_at_least_50ms_count << '/'
				<< runtime_status.wait_get_poses_at_least_100ms_count << '\n';
			const auto pending_average = runtime_status.prepared_pair_ready_sample_count != 0
				? runtime_status.prepared_pair_pending_presents_total /
					runtime_status.prepared_pair_ready_sample_count
				: 0;
			const auto ready_average = runtime_status.prepared_pair_ready_sample_count != 0
				? runtime_status.prepared_pair_ready_total_us /
					runtime_status.prepared_pair_ready_sample_count
				: 0;
			output << "  openvr_pair_ready: samples="
				<< runtime_status.prepared_pair_ready_sample_count;
			output << " pending_presents(last/max/avg)="
				<< runtime_status.prepared_pair_pending_presents_last << '/'
				<< runtime_status.prepared_pair_pending_presents_max << '/'
				<< pending_average;
			output << " us(last/max/avg)=" << runtime_status.prepared_pair_ready_last_us
				<< '/' << runtime_status.prepared_pair_ready_max_us << '/'
				<< ready_average << '\n';
		}
		output << "  present_owner_transaction: active="
			<< yes_no(runtime_status.present_owner_transaction_active);
		output << " frame=" << runtime_status.present_owner_transaction_frame;
		output << " generation=" << runtime_status.present_owner_transaction_generation;
		output << " thread=" << runtime_status.present_owner_transaction_thread_id;
		output << " pre=" << runtime_status.present_owner_pre_count;
		output << " post=" << runtime_status.present_owner_post_count;
		output << " last_completed=" << runtime_status.present_owner_last_completed_frame;
		output << " last_hresult=0x" << std::hex << std::uppercase
			<< static_cast<std::uint32_t>(runtime_status.present_owner_last_post_hresult);
		output << std::dec << std::nouppercase << '\n';
		output << "  submission: transport=" << available(runtime_status.graphics_transport);
		output << " game_device=" << yes_no(runtime_status.submission_on_game_device);
		output << " adapter_preflight=" << runtime_status.adapter_preflight_successes << '/';
		output << runtime_status.adapter_preflight_attempts;
		output << " failures=" << runtime_status.adapter_preflight_failures << '\n';
		output << "  direct_submission: pairs_acquired=" << runtime_status.direct_pairs_acquired;
		output << " pairs_retired=" << runtime_status.direct_pairs_retired;
		output << " pairs_deferred=" << runtime_status.direct_pairs_deferred;
		output << " submit_attempts=" << runtime_status.direct_submit_attempts;
		output << " submit_failures=" << runtime_status.direct_submit_failures << '\n';
		if (runtime_status.backend_name == "openvr")
		{
			const auto submit_average = runtime_status.submit_call_count != 0
				? runtime_status.submit_total_us / runtime_status.submit_call_count
				: 0;
			const auto retirement_average = runtime_status.submit_to_retirement_count != 0
				? runtime_status.submit_to_retirement_total_us /
					runtime_status.submit_to_retirement_count
				: 0;
			output << "  openvr_submit_timing: calls=" << runtime_status.submit_call_count;
			output << " us(last/max/avg)=" << runtime_status.submit_last_us << '/'
				<< runtime_status.submit_max_us << '/' << submit_average;
			output << " retirement(samples/last/max/avg_us)="
				<< runtime_status.submit_to_retirement_count << '/'
				<< runtime_status.submit_to_retirement_last_us << '/'
				<< runtime_status.submit_to_retirement_max_us << '/'
				<< retirement_average << '\n';
			output << "  openvr_compositor_timing: queries="
				<< runtime_status.compositor_frame_timing_query_count;
			output << " valid=" << yes_no(runtime_status.compositor_frame_timing_valid);
			output << " frame=" << runtime_status.compositor_frame_timing_frame_index;
			output << " dropped=" << runtime_status.compositor_frame_timing_dropped_frames;
			output << " mispresented="
				<< runtime_status.compositor_frame_timing_mispresented;
			output << " ms(client/present/wait/submit/pre_gpu/post_gpu/total_gpu)="
				<< runtime_status.compositor_frame_timing_client_interval_ms << '/'
				<< runtime_status.compositor_frame_timing_present_cpu_ms << '/'
				<< runtime_status.compositor_frame_timing_wait_for_present_cpu_ms << '/'
				<< runtime_status.compositor_frame_timing_submit_frame_ms << '/'
				<< runtime_status.compositor_frame_timing_pre_submit_gpu_ms << '/'
				<< runtime_status.compositor_frame_timing_post_submit_gpu_ms << '/'
				<< runtime_status.compositor_frame_timing_total_render_gpu_ms << '\n';
		}
		const auto& format_probe = runtime_status.submit_format_probe;
		output << "  format_probe: state=" << available(format_probe.state);
		output << " active=" << yes_no(format_probe.active);
		output << " complete=" << yes_no(format_probe.complete);
		output << " current=" << format_probe.current_candidate;
		output << " selected=" << format_probe.selected_format;
		output << " attempts=" << format_probe.candidate_attempts;
		output << " rejections=" << format_probe.candidate_rejections;
		output << " clears=" << format_probe.clear_last_frame_calls << '\n';
		for (std::size_t index{}; index < format_probe.candidates.size(); ++index)
		{
			const auto& candidate = format_probe.candidates[index];
			output << "    candidate[" << index << "]: format=" << candidate.format;
			output << " check=0x" << std::hex << std::uppercase
				<< static_cast<std::uint32_t>(candidate.check_format_result);
			output << " support=0x" << candidate.format_support;
			output << std::dec << std::nouppercase;
			output << " ring=" << yes_no(candidate.ring_ready);
			output << " create_error=" << available(candidate.creation_error);
			output << " submit=" << candidate.left_submit_result << '/'
				<< candidate.right_submit_result;
			output << " accepted_mask=0x" << std::hex << candidate.accepted_eye_mask;
			output << std::dec;
			output << " retired=" << yes_no(candidate.retired) << '\n';
		}
		output << "  direct_threads: renderer=" << runtime_status.direct_renderer_thread_id;
		output << " scene_owner=" << runtime_status.direct_scene_owner_thread_id;
		output << " present_owner=" << runtime_status.direct_present_owner_thread_id;
		output << " split=" << yes_no(runtime_status.direct_renderer_thread_id != 0 &&
			runtime_status.direct_present_owner_thread_id != 0 &&
			runtime_status.direct_renderer_thread_id != runtime_status.direct_present_owner_thread_id);
		output << " owner_contract=" << yes_no(runtime_status.direct_present_owner_contract_valid);
		output << " violations=" << runtime_status.direct_present_owner_contract_violations << '\n';
		output << "  scene_mode: requested=" << scene_mode_name(runtime_status.requested_scene_mode);
		output << " effective=" << scene_mode_name(runtime_status.effective_scene_mode) << '\n';
		output << "  compositor: prepare=" << runtime_status.compositor_prepare_count;
		output << " render=" << runtime_status.compositor_render_count;
		output << " source_misses=" << runtime_status.compositor_source_miss_count << '\n';
		output << "  compositor_error=" << available(runtime_status.last_compositor_error) << '\n';
		if (runtime_status.backend_name == "openvr")
		{
			if (runtime_status.submission_on_game_device &&
				runtime_status.graphics_transport == "h2_device_direct")
			{
				output << "  capture: disabled_by=same_device_direct_submission\n";
			}
			else
			{
				output << "  capture: disabled_by=gpu_transport_unarmed\n";
			}
		}
		else
		{
			const auto& capture = runtime_status.capture;
			output << "  capture: produced=" << capture.produced;
			output << " ready=" << capture.ready;
			output << " acquired=" << capture.acquired;
			output << " released=" << capture.released;
			output << " dropped=" << capture.dropped << '\n';
			output << "  capture: query_pending=" << capture.query_pending;
			output << " slot_exhausted=" << capture.slot_exhausted;
			output << " stale_generation=" << capture.stale_generation;
			output << " invalidated=" << capture.invalidated << '\n';
			output << "  capture: shared_opened=" << capture.shared_opened;
			output << " shared_reused=" << capture.shared_import_reused;
			output << " direct_acquired=" << capture.native_direct_acquired;
			output << " shared_failed=" << capture.shared_failed;
			output << " cpu_requested=" << capture.cpu_fallback_requested;
			output << " cpu_ready=" << capture.cpu_fallback_ready;
			output << " cpu_consumed=" << capture.cpu_fallback_consumed << '\n';
			output << "  capture_native: produced=" << capture.native_produced;
			output << " pairs_acquired=" << capture.native_pairs_acquired;
			output << " pair_misses=" << capture.native_pair_misses << '\n';
		}
		output << "  worker: active=" << yes_no(runtime_status.worker_active);
		output << " phase=" << available(runtime_status.worker_phase);
		output << " wakeups=" << runtime_status.worker_wakeup_count;
		output << " presents=" << runtime_status.worker_present_count;
		output << " backend_calls=" << runtime_status.worker_backend_call_count;
		output << " graphics=" << yes_no(runtime_status.worker_graphics_available);
		output << " last_sequence=" << runtime_status.worker_last_present_sequence;
		output << " last_generation=" << runtime_status.worker_last_generation;
		output << " last_config=" << runtime_status.worker_last_configuration_generation << '\n';
		output << "  worker_operation=" << available(runtime_status.worker_current_operation);
		output << " last_completed=" << available(runtime_status.worker_last_completed_operation);
		output << " progress_age_ms=" << runtime_status.worker_progress_age_ms;
		output << " stalled=" << yes_no(runtime_status.worker_progress_stalled);
		output << " watchdog_count=" << runtime_status.worker_watchdog_count << '\n';
		output << "  worker_error=" << available(runtime_status.worker_last_error) << '\n';
		output << "head pose bridge:\n";
		output << "  target_matched=" << yes_no(head_status.target_matched);
		output << " enabled=" << yes_no(head_status.enabled);
		output << " pose_available=" << yes_no(head_status.pose_available);
		output << " recenter_pending=" << yes_no(head_status.recenter_pending);
		output << " camera_applied=" << yes_no(head_status.camera_applied) << '\n';
		output << "  world_scale=" << head_status.world_scale;
		output << " pose_publications=" << head_status.pose_publications;
		output << " camera_applications=" << head_status.camera_applications;
		output << " recenter_count=" << head_status.recenter_count;
		output << " invalid=" << head_status.invalid_pose_count;
		output << " composition_failures=" << head_status.composition_failure_count << '\n';
		output << "  roll_degrees: local=" << head_status.local_roll_degrees;
		output << " input_horizon=" << head_status.input_horizon_roll_degrees;
		output << " output_horizon=" << head_status.output_horizon_roll_degrees << '\n';
		output << "  absolute_pose_degrees: reference_pitch=";
		output << head_status.reference_absolute_pitch_degrees;
		output << " reference_roll=" << head_status.reference_absolute_roll_degrees;
		output << " current_pitch=" << head_status.current_absolute_pitch_degrees;
		output << " current_roll=" << head_status.current_absolute_roll_degrees << '\n';
		output << "  base_camera: policy=yaw_only input_pitch=";
		output << head_status.input_pitch_degrees;
		output << " base_yaw=" << head_status.base_yaw_degrees;
		output << " output_pitch=" << head_status.output_pitch_degrees;
		output << " yaw_from_left=" << head_status.yaw_from_left_count;
		output << " yaw_failures=" << head_status.yaw_extraction_failure_count << '\n';
		output << "  roll_peak_abs_degrees: local=" << head_status.max_abs_local_roll_degrees;
		output << " input_horizon=" << head_status.max_abs_input_horizon_roll_degrees;
		output << " output_horizon=" << head_status.max_abs_output_horizon_roll_degrees << '\n';
		output << "  roll_range_degrees: local=" << head_status.min_local_roll_degrees << "..";
		output << head_status.max_local_roll_degrees << " input_horizon=";
		output << head_status.min_input_horizon_roll_degrees << "..";
		output << head_status.max_input_horizon_roll_degrees << " output_horizon=";
		output << head_status.min_output_horizon_roll_degrees << "..";
		output << head_status.max_output_horizon_roll_degrees << '\n';
		output << "  basis_error: input=" << head_status.input_basis_error;
		output << " output=" << head_status.output_basis_error;
		output << " max_output=" << head_status.max_output_basis_error << '\n';
		output << "  local_position_units=" << head_status.local_position_units[0] << ',';
		output << head_status.local_position_units[1] << ',' << head_status.local_position_units[2] << '\n';
		output << "  roomscale_origin_offset_m=" << head_status.roomscale_offset_meters[0] << ',';
		output << head_status.roomscale_offset_meters[1] << ',' << head_status.roomscale_offset_meters[2] << '\n';
		for (std::size_t row{}; row < head_status.local_orientation.size(); ++row)
		{
			output << "  local_axis[" << row << "]=" << head_status.local_orientation[row][0] << ',';
			output << head_status.local_orientation[row][1] << ',';
			output << head_status.local_orientation[row][2] << '\n';
		}
	}
}
