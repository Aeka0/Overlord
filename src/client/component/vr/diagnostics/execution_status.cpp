#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "../engine_stereo_draw_indexed.hpp"
#include "../engine_stereo_dynamic_arena.hpp"
#include "../engine_stereo_execution.hpp"
#include "../engine_stereo_output_merger.hpp"
#include "../engine_stereo_owner_pass.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr::diagnostics::detail
{
	void append_execution_status(std::ostringstream& output,
		const engine_stereo_output_merger::status& output_merger_status,
		const engine_stereo_draw_indexed::status& draw_indexed_status,
		const engine_stereo_execution::status& execution_status,
		const engine_stereo_owner_pass::report& owner_pass_status)
	{
		output << "  backend_output_merger: state="
			<< engine_stereo_output_merger::to_string(output_merger_status.state);
		output << " hook=" << (output_merger_status.hook_installed ? "yes" : "no");
		output << " extended_hooks="
			<< (output_merger_status.extended_hooks_installed ? "yes" : "no");
		output << " hook_target=0x" << std::hex << output_merger_status.hook_target;
		output << " uav_hook_target=0x"
			<< output_merger_status.unordered_access_hook_target;
		output << " clear_hook_target=0x"
			<< output_merger_status.clear_state_hook_target;
		output << " clear_rtv_hook_target=0x"
			<< output_merger_status.clear_render_target_hook_target;
		output << " clear_dsv_hook_target=0x"
			<< output_merger_status.clear_depth_stencil_hook_target;
		output << " context=0x" << output_merger_status.expected_context << std::dec;
		output << " generation=" << output_merger_status.device_generation;
		output << " hook_failures=" << output_merger_status.hook_failures;
		output << " attempts=" << output_merger_status.attempts;
		output << " complete=" << output_merger_status.completions;
		output << " failures=" << output_merger_status.failures;
		output << " binds=" << output_merger_status.bind_calls;
		output << " nonnull=" << output_merger_status.nonnull_bind_calls;
		output << " context_mismatch=" << output_merger_status.context_mismatches;
		output << " thread_mismatch=" << output_merger_status.thread_mismatches;
		output << " query_failures=" << output_merger_status.query_failures;
		output << " overflow=" << output_merger_status.overflows;
		output << " metadata_queries=" << output_merger_status.metadata_queries;
		output << " uav_calls=" << output_merger_status.unordered_access_calls;
		output << " clear_calls=" << output_merger_status.clear_state_calls;
		output << " clear_rtv_calls="
			<< output_merger_status.clear_render_target_calls;
		output << " clear_dsv_calls="
			<< output_merger_status.clear_depth_stencil_calls;
		output << " binding_invalidations="
			<< output_merger_status.binding_invalidations << '\n';
		output << "  backend_draw_indexed: state="
			<< engine_stereo_draw_indexed::to_string(draw_indexed_status.state);
		output << " hook=" << (draw_indexed_status.hook_installed ? "yes" : "no");
		output << " hook_target=0x" << std::hex << draw_indexed_status.hook_target;
		output << " context=0x" << draw_indexed_status.expected_context << std::dec;
		output << " generation=" << draw_indexed_status.device_generation;
		output << " hook_failures=" << draw_indexed_status.hook_failures;
		output << " attempts=" << draw_indexed_status.attempts;
		output << " complete=" << draw_indexed_status.completions;
		output << " failures=" << draw_indexed_status.failures;
		output << " draws=" << draw_indexed_status.draw_calls;
		output << " context_mismatch=" << draw_indexed_status.context_mismatches;
		output << " thread_mismatch=" << draw_indexed_status.thread_mismatches;
		output << " invalid_arguments=" << draw_indexed_status.invalid_arguments;
		output << " overflow=" << draw_indexed_status.overflows << '\n';
		output << "  backend_execution_census: state="
			<< engine_stereo_execution::to_string(execution_status.state);
		output << " error=" << engine_stereo_execution::to_string(execution_status.error);
		output << " hooks=" << (execution_status.hooks_installed ? "yes" : "no");
		output << " installed=" << execution_status.installed_hook_count;
		output << " targets_distinct="
			<< (execution_status.targets_distinct ? "yes" : "no");
		output << " permanent_install_failure="
			<< (execution_status.installation_permanently_failed ? "yes" : "no");
		output << " draw_indexed_external="
			<< (execution_status.draw_indexed_forwarding_external ? "yes" : "no");
		output << " draw_indexed_forwarded="
			<< execution_status.draw_indexed_forwarded_calls;
		output << " context=0x" << std::hex << execution_status.expected_context
			<< " om_target=0x" << execution_status.output_merger_target
			<< " om_uav_target=0x"
			<< execution_status.output_merger_unordered_access_target
			<< " clear_state_target=0x" << execution_status.clear_state_target
			<< std::dec;
		output << " generation=" << execution_status.device_generation;
		output << " hook_failures=" << execution_status.hook_failures;
		output << " attempts=" << execution_status.attempts;
		output << " complete=" << execution_status.completions;
		output << " failures=" << execution_status.failures;
		output << " events=" << execution_status.recorded_events << '/'
			<< execution_status.event_reservations;
		output << " classifier_events=" << execution_status.classifier_events;
		output << " frame_events=" << execution_status.frame_events;
		output << " expected_context_frame_events="
			<< execution_status.expected_context_frame_events;
		output << " backend_scoped=" << execution_status.backend_scoped_events;
		output << " backend_scoped_frame="
			<< execution_status.backend_scoped_frame_events;
		output << " backend_unscoped_frame="
			<< execution_status.backend_unscoped_frame_events;
		output << " backend_thread_mismatch="
			<< execution_status.backend_thread_mismatches;
		output << " backend_records="
			<< execution_status.distinct_backend_records;
		output << " scene_owner_scoped="
			<< execution_status.scene_owner_scoped_events;
		output << " scene_owner_scoped_frame="
			<< execution_status.scene_owner_scoped_frame_events;
		output << " scene_owner_unscoped_frame="
			<< execution_status.scene_owner_unscoped_frame_events;
		output << " scene_owner_thread_mismatch="
			<< execution_status.scene_owner_thread_mismatches;
		output << " scene_owner_records="
			<< execution_status.distinct_scene_owner_records;
		output << " stack_samples=" << execution_status.call_stack_samples;
		output << " stack_failures="
			<< execution_status.call_stack_capture_failures;
		output << " stack_overflow="
			<< execution_status.call_stack_key_overflows;
		output << " admission_collisions="
			<< execution_status.admission_collisions;
		output << " overflow=" << execution_status.overflows;
		output << " foreign_context=" << execution_status.foreign_context_events;
		output << " opaque_execute_command_lists="
			<< execution_status.opaque_execute_command_lists;
		output << " known_conversion(recording/execute/replay)=" << execution_status.known_conversion_recordings
			<< '/' << execution_status.known_conversion_executions << '/' << execution_status.known_conversion_replays;
		output << " deferred_opaque="
			<< (execution_status.deferred_execution_opaque ? "yes" : "no");
		output << " identity_truncated="
			<< (execution_status.identity_truncated ? "yes" : "no");
		output << " classifier_thread_mismatch="
			<< execution_status.classifier_thread_mismatches;
		output << " contexts=" << execution_status.distinct_contexts;
		output << " threads=" << execution_status.distinct_threads;
		output << " writers=" << execution_status.active_writers;
		output << " max_writers=" << execution_status.maximum_active_writers;
		output << " classifier_record=0x" << std::hex
			<< execution_status.classifier_record << std::dec;
		output << " type=" << execution_status.classifier_record_type;
		output << " classifier_thread=" << execution_status.classifier_thread_id;
		output << " classifier_qpc=" << execution_status.classifier_begin_qpc << "->"
			<< execution_status.classifier_end_qpc;
		output << " frame=" << execution_status.frame_start_present_post << "->"
			<< execution_status.frame_end_present_pre << "->"
			<< execution_status.frame_end_present_post;
		output << " present_result=0x" << std::hex
			<< static_cast<std::uint32_t>(execution_status.frame_present_result)
			<< std::dec;
		output << " frame_threads=" << execution_status.frame_start_thread_id << "->"
			<< execution_status.frame_end_thread_id << '\n';
		output << "  backend_execution_hooks:";
		for (std::size_t index{}; index < engine_stereo_execution::api_count; ++index)
		{
			const auto operation = static_cast<engine_stereo_execution::api>(index);
			output << ' ' << engine_stereo_execution::to_string(operation)
				<< "=0x" << std::hex << execution_status.hook_targets[index]
				<< std::dec;
		}
		output << '\n';
		output << "  backend_execution_api:";
		for (std::size_t index{}; index < engine_stereo_execution::api_count; ++index)
		{
			const auto operation = static_cast<engine_stereo_execution::api>(index);
			output << ' ' << engine_stereo_execution::to_string(operation) << '='
				<< execution_status.per_api[index] << '/'
				<< execution_status.classifier_per_api[index] << '/'
				<< execution_status.frame_per_api[index];
		}
		output << '\n';
		output << "  backend_owner_stereo_pass: state="
			<< engine_stereo_owner_pass::to_string(owner_pass_status.state);
		output << " error=" << engine_stereo_owner_pass::to_string(owner_pass_status.error);
		output << " attempts=" << owner_pass_status.attempts;
		output << " complete=" << owner_pass_status.completions;
		output << " failures=" << owner_pass_status.failures;
		output << " pair=" << owner_pass_status.pair_id;
		output << " publication=" << owner_pass_status.publication;
		output << " eyes=0x" << std::hex << owner_pass_status.completed_eye_mask;
		output << " target=" << std::dec << owner_pass_status.final_target_id;
		output << " size=" << owner_pass_status.width << 'x' << owner_pass_status.height;
		output << " format=" << owner_pass_status.format;
		output << " mips=" << owner_pass_status.mip_levels;
		output << " array=" << owner_pass_status.array_size;
		output << " samples=" << owner_pass_status.sample_count << ':'
			<< owner_pass_status.sample_quality;
		output << " usage=" << owner_pass_status.usage;
		output << " bind=0x" << std::hex << owner_pass_status.bind_flags;
		output << " cpu=0x" << owner_pass_status.cpu_access_flags;
		output << " misc=0x" << owner_pass_status.misc_flags;
		output << " source=0x" << std::hex << owner_pass_status.source_textures[0]
			<< ",0x" << owner_pass_status.source_textures[1];
		output << " hashes=0x" << owner_pass_status.content_hashes[0]
			<< ",0x" << owner_pass_status.content_hashes[1];
		output << std::dec << " nonzero=" << owner_pass_status.nonzero_bytes[0]
			<< ',' << owner_pass_status.nonzero_bytes[1];
		output << " distinct=" << (owner_pass_status.eyes_distinct ? "yes" : "no");
		output << " query_polls=" << owner_pass_status.query_polls;
		output << " create=0x" << std::hex
			<< static_cast<std::uint32_t>(owner_pass_status.staging_results[0])
			<< ",0x" << static_cast<std::uint32_t>(owner_pass_status.staging_results[1]);
		output << " query=0x" << static_cast<std::uint32_t>(owner_pass_status.query_result)
			<< "/0x" << static_cast<std::uint32_t>(owner_pass_status.query_poll_result);
		output << " map=0x" << static_cast<std::uint32_t>(owner_pass_status.map_results[0])
			<< ",0x" << static_cast<std::uint32_t>(owner_pass_status.map_results[1])
			<< " removed=0x" << static_cast<std::uint32_t>(
				owner_pass_status.device_removed_reason) << std::dec << '\n';
		output << "  backend_owner_stereo_transport: attempts="
			<< owner_pass_status.production_attempts;
		output << " complete=" << owner_pass_status.production_completions;
		output << " failures=" << owner_pass_status.production_failures;
		output << " eye_copies=" << owner_pass_status.production_eye_copies;
		output << " last_pair=" << owner_pass_status.production_last_pair << '\n';
		output << "  native_display_transform: path=h2_postfx encoding=display_srgb submit_decode=srgb_to_linear complete="
			<< owner_pass_status.display_transform_completions
			<< " routes=" << owner_pass_status.display_routes[0].source << "->"
			<< owner_pass_status.display_routes[0].destination << "/"
			<< owner_pass_status.display_routes[1].source << "->"
			<< owner_pass_status.display_routes[1].destination
			<< " failures=" << owner_pass_status.display_transform_failures
			<< " format=" << owner_pass_status.display_format
			<< " bind=0x" << std::hex << owner_pass_status.display_bind_flags << std::dec
			<< " last_error=" << owner_pass_status.display_transform_error << '\n';
		output << "  backend_owner_last_failure: reason="
			<< engine_stereo_owner_pass::to_string(owner_pass_status.last_failure);
		output << " production=" << (owner_pass_status.failure_in_production ? "yes" : "no");
		output << " pair=" << owner_pass_status.failure_pair;
		output << " eye=" << owner_pass_status.failure_eye;
		output << " completed_mask=0x" << std::hex
			<< owner_pass_status.failure_completed_mask << std::dec;
		output << " model_other=" << owner_pass_status.failure_model_other[0]
			<< '/' << owner_pass_status.failure_model_other[1] << '\n';
		output << "  backend_dynamic_index_scope: captures="
			<< owner_pass_status.dynamic_index_captures;
		output << " rebases=" << owner_pass_status.dynamic_index_rebases;
		output << " restores=" << owner_pass_status.dynamic_index_restores;
		output << " validations=" << owner_pass_status.dynamic_index_validations;
		output << " failures=" << owner_pass_status.dynamic_index_failures;
		output << " last_boundaries="
			<< owner_pass_status.dynamic_index_last_left_boundaries << '/'
			<< owner_pass_status.dynamic_index_last_right_boundaries;
		output << " data=0x" << std::hex
			<< owner_pass_status.dynamic_index_data_identity << std::dec;
		output << " last_failure=" << engine_stereo_dynamic_arena::to_string(
			owner_pass_status.dynamic_index_last_failure) << '\n';
		output << "  backend_dynamic_index_boundary: scope=scene_geometry_executor\n";
		if (owner_pass_status.last_failure == engine_stereo_owner_pass::failure::dynamic_mesh)
		{
			const auto& calls = owner_pass_status.dynamic_index_failure_trace;
			output << "  backend_dynamic_index_failure_calls: pair="
				<< owner_pass_status.failure_pair << " counts=" << calls.counts[0]
				<< '/' << calls.counts[1] << " dropped=" << calls.dropped[0]
				<< '/' << calls.dropped[1] << '\n';
			for (std::size_t index{}; index < calls.eyes[0].size() &&
				(index < calls.counts[0] || index < calls.counts[1]); ++index)
			{
				output << "    view_boundary=" << index;
				for (std::size_t eye{}; eye < 2; ++eye)
				{
					output << (eye == 0 ? " left=" : " right=");
					if (index >= calls.counts[eye]) { output << "absent"; continue; }
					const auto& entry = calls.eyes[eye][index];
					output << "0x" << std::hex << entry.caller << "@0x"
						<< entry.backend << std::dec << ":upload"
						<< static_cast<unsigned>(entry.upload_phase)
						<< (entry.geometry ? ":geometry" : ":auxiliary");
				}
				output << '\n';
			}
		}
	}
}
