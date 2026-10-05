#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "../engine_stereo_gpu_census.hpp"
#include "../engine_stereo_gpu_timing.hpp"
#include "../engine_stereo_material_buffer_probe.hpp"
#include "../engine_stereo_owner_pass.hpp"
#include "../engine_stereo_particle_buffer_probe.hpp"
#include "../engine_stereo_view.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr::diagnostics::detail
{
	void append_owner_status(std::ostringstream& output,
		const engine_stereo_owner_pass::report& owner_pass_status,
		const engine_stereo_material_buffer_probe::report& material_buffer_status,
		const engine_stereo_particle_buffer_probe::report& particle_buffer_status)
	{
		output << "  backend_owner_context_lock: acquires="
			<< owner_pass_status.production_context_lock_acquires;
		output << " failures=" << owner_pass_status.production_context_lock_failures;
		output << " result=0x" << std::hex << std::uppercase
			<< owner_pass_status.production_context_lock_last_result;
		output << std::dec << std::nouppercase;
		output << " wait_us=" << owner_pass_status.production_context_lock_wait_total_us;
		output << "/" << owner_pass_status.production_context_lock_wait_max_us;
		output << "/" << owner_pass_status.production_context_lock_wait_last_us << '\n';
		const auto transaction_timing_average =
			owner_pass_status.production_transaction_timing_samples != 0
			? owner_pass_status.production_transaction_timing_total_us /
				owner_pass_status.production_transaction_timing_samples
			: 0;
		output << "  backend_owner_timing: transaction(samples/last/max/avg_us)="
			<< owner_pass_status.production_transaction_timing_samples << '/'
			<< owner_pass_status.production_transaction_timing_last_us << '/'
			<< owner_pass_status.production_transaction_timing_max_us << '/'
			<< transaction_timing_average;
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			const auto eye_timing_average =
				owner_pass_status.production_eye_timing_samples[eye] != 0
				? owner_pass_status.production_eye_timing_total_us[eye] /
					owner_pass_status.production_eye_timing_samples[eye]
				: 0;
			output << " eye[" << eye << "]="
				<< owner_pass_status.production_eye_timing_samples[eye] << '/'
				<< owner_pass_status.production_eye_timing_last_us[eye] << '/'
				<< owner_pass_status.production_eye_timing_max_us[eye] << '/'
				<< eye_timing_average;
		}
		output << '\n';
		output << "  backend_owner_stage_timing:";
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			const auto invoke_average =
				owner_pass_status.production_owner_invoke_timing_samples[eye] != 0
				? owner_pass_status.production_owner_invoke_timing_total_us[eye] /
					owner_pass_status.production_owner_invoke_timing_samples[eye]
				: 0;
			const auto dynamic_average =
				owner_pass_status.production_dynamic_view_copy_timing_samples[eye] != 0
				? owner_pass_status.production_dynamic_view_copy_timing_total_us[eye] /
					owner_pass_status.production_dynamic_view_copy_timing_samples[eye]
				: 0;
			const auto copy_average =
				owner_pass_status.production_native_copy_timing_samples[eye] != 0
				? owner_pass_status.production_native_copy_timing_total_us[eye] /
					owner_pass_status.production_native_copy_timing_samples[eye]
				: 0;
			output << " eye[" << eye << "]_invoke(samples/last/max/avg_us)="
				<< owner_pass_status.production_owner_invoke_timing_samples[eye] << '/'
				<< owner_pass_status.production_owner_invoke_timing_last_us[eye] << '/'
				<< owner_pass_status.production_owner_invoke_timing_max_us[eye] << '/'
				<< invoke_average;
			output << " dynamic="
				<< owner_pass_status.production_dynamic_view_copy_timing_samples[eye] << '/'
				<< owner_pass_status.production_dynamic_view_copy_timing_last_us[eye] << '/'
				<< owner_pass_status.production_dynamic_view_copy_timing_max_us[eye] << '/'
				<< dynamic_average;
			output << " native_copy="
				<< owner_pass_status.production_native_copy_timing_samples[eye] << '/'
				<< owner_pass_status.production_native_copy_timing_last_us[eye] << '/'
				<< owner_pass_status.production_native_copy_timing_max_us[eye] << '/'
				<< copy_average;
		}
		constexpr auto auxiliary=auxiliary_scene::view_index;
		output << " auxiliary_invoke(samples/last/max_us)="
			<< owner_pass_status.production_owner_invoke_timing_samples[auxiliary] << '/'
			<< owner_pass_status.production_owner_invoke_timing_last_us[auxiliary] << '/'
			<< owner_pass_status.production_owner_invoke_timing_max_us[auxiliary];
		const auto restore_average =
			owner_pass_status.production_dynamic_restore_timing_samples != 0
			? owner_pass_status.production_dynamic_restore_timing_total_us /
				owner_pass_status.production_dynamic_restore_timing_samples
			: 0;
		output << " restore="
			<< owner_pass_status.production_dynamic_restore_timing_samples << '/'
			<< owner_pass_status.production_dynamic_restore_timing_last_us << '/'
			<< owner_pass_status.production_dynamic_restore_timing_max_us << '/'
			<< restore_average << '\n';
		const auto gpu_timing = engine_stereo_gpu_timing::get_report();
		output << "  backend_gpu_timing: state="
			<< engine_stereo_gpu_timing::to_string(gpu_timing.state);
		output << " error=" << engine_stereo_gpu_timing::to_string(gpu_timing.error);
		output << " production="
			<< (engine_stereo_gpu_timing::instrumentation_enabled ? "enabled" : "hard_unarmed");
		output << " protocol=one_shot_donotflush";
		output << " prepare=" << gpu_timing.prepare_attempts << '/'
			<< gpu_timing.prepare_completions << '/' << gpu_timing.prepare_failures;
		output << " eligibility=" << gpu_timing.eligibility_checks << '/'
			<< gpu_timing.eligibility_skips;
		output << " capture=" << gpu_timing.capture_attempts << '/'
			<< gpu_timing.capture_completions << '/' << gpu_timing.capture_failures;
		output << " pair=" << gpu_timing.pair_id;
		output << " generation=" << gpu_timing.device_generation;
		output << " owner_thread=" << gpu_timing.owner_thread;
		output << " marker_mask=0x" << std::hex << gpu_timing.marker_mask;
		output << " device=0x" << gpu_timing.device;
		output << " context=0x" << gpu_timing.context << std::dec << '\n';
		output << "  backend_gpu_timing_retirement: observed="
			<< yes_no(gpu_timing.retirement_observed);
		output << " retired_pair=" << gpu_timing.retired_pair;
		output << " polls=" << gpu_timing.retirement_polls;
		output << " not_ready=" << gpu_timing.not_ready_polls;
		output << " get_data_calls=" << gpu_timing.get_data_calls;
		output << " flags=0x" << std::hex << gpu_timing.get_data_flags << std::dec;
		output << " disjoint=" << yes_no(gpu_timing.disjoint);
		output << " frequency=" << gpu_timing.frequency << '\n';
		output << "  backend_gpu_timing_ns: left_owner=" << gpu_timing.left_owner_ns;
		output << " left_conversion=" << gpu_timing.left_conversion_ns;
		output << " owner_to_left_conversion="
			<< gpu_timing.owner_to_left_conversion_ns;
		output << " between_eyes=" << gpu_timing.between_eyes_ns;
		output << " right_owner=" << gpu_timing.right_owner_ns;
		output << " right_conversion=" << gpu_timing.right_conversion_ns;
		output << " owner_to_right_conversion="
			<< gpu_timing.owner_to_right_conversion_ns;
		output << " pair_window=" << gpu_timing.pair_window_ns << '\n';
		output << "  backend_gpu_timing_query_create: disjoint=0x" << std::hex
			<< static_cast<std::uint32_t>(gpu_timing.disjoint_create_result)
			<< " timestamps=";
		for (std::size_t index{}; index < gpu_timing.timestamp_create_results.size();
			++index)
		{
			if (index != 0) output << ',';
			output << "0x" << static_cast<std::uint32_t>(
				gpu_timing.timestamp_create_results[index]);
		}
		output << std::dec << '\n';
		output << "  backend_gpu_timing_query_read: disjoint=0x" << std::hex
			<< static_cast<std::uint32_t>(gpu_timing.disjoint_get_data_result)
			<< " timestamps=";
		for (std::size_t index{}; index < gpu_timing.timestamp_get_data_results.size();
			++index)
		{
			if (index != 0) output << ',';
			output << "0x" << static_cast<std::uint32_t>(
				gpu_timing.timestamp_get_data_results[index]);
		}
		output << std::dec << '\n';
		output << "  backend_temporal_history: ready="
			<< (owner_pass_status.temporal_history_ready ? "yes" : "no")
			<< " preparations=" << owner_pass_status.temporal_history_preparations
			<< " seeds=" << owner_pass_status.temporal_history_seeds
			<< " commits=" << owner_pass_status.temporal_history_commits
			<< " failures=" << owner_pass_status.temporal_history_failures
			<< " reset(requested/applied/pending)="
			<< owner_pass_status.temporal_history_reset_requests << '/'
			<< owner_pass_status.temporal_history_reset_applications << '/'
			<< (owner_pass_status.temporal_history_reset_pending ? "yes" : "no")
			<< " last_pair=" << owner_pass_status.temporal_history_last_pair
			<< " current_frame_diagnostic="
			<< (owner_pass_status.temporal_history_current_frame_diagnostic ?
				"enabled" : "disabled")
			<< " diagnostic_pairs="
			<< owner_pass_status.temporal_history_current_frame_pairs << '\n';
		output << "  backend_material_reuse_diagnostic: mode="
			<< (owner_pass_status.material_reuse_left_diagnostic ?
				"reuse_left" : "disabled")
			<< " pairs=" << owner_pass_status.material_reuse_left_pairs
			<< " captures=" << owner_pass_status.material_reuse_left_captures
			<< " replacements="
			<< owner_pass_status.material_reuse_left_replacements
			<< " failures=" << owner_pass_status.material_reuse_left_failures
			<< " last_pair=" << owner_pass_status.material_reuse_left_last_pair << '\n';
		output << "  backend_material_buffer_readback: state="
			<< engine_stereo_material_buffer_probe::to_string(
				material_buffer_status.current);
		output << " error=" << engine_stereo_material_buffer_probe::to_string(
			material_buffer_status.error);
		output << " pair=" << material_buffer_status.pair_id;
		output << " generation=" << material_buffer_status.device_generation;
		output << " context/thread/eye=0x" << std::hex
			<< material_buffer_status.context << std::dec << '/'
			<< material_buffer_status.owner_thread << '/'
			<< material_buffer_status.active_eye;
		output << " mask=0x" << std::hex
			<< static_cast<std::uint32_t>(material_buffer_status.completed_eye_mask)
			<< std::dec;
		output << " captures=" << material_buffer_status.captures[0] << '/'
			<< material_buffer_status.captures[1];
		output << " capture(attempt/complete/reject/overflow)="
			<< material_buffer_status.capture_attempts << '/'
			<< material_buffer_status.capture_completions << '/'
			<< material_buffer_status.capture_contract_rejections << '/'
			<< material_buffer_status.capture_overflows;
		output << " bound(attempt/complete/dedup)="
			<< material_buffer_status.bound_capture_attempts << '/'
			<< material_buffer_status.bound_capture_completions << '/'
			<< material_buffer_status.capture_deduplications;
		output << " references(attempt/complete/overflow)="
			<< material_buffer_status.reference_attempts << '/'
			<< material_buffer_status.reference_completions << '/'
			<< material_buffer_status.reference_overflows << '\n';
		output << "    material_buffer_retirement: polls/not_ready="
			<< material_buffer_status.retirement_polls << '/'
			<< material_buffer_status.not_ready_polls;
		output << " create(staging/query)=0x" << std::hex
			<< static_cast<std::uint32_t>(material_buffer_status.staging_create_result)
			<< "/0x" << static_cast<std::uint32_t>(
				material_buffer_status.query_create_result);
		output << " retire(query/map)=0x" << static_cast<std::uint32_t>(
			material_buffer_status.query_result) << "/0x"
			<< static_cast<std::uint32_t>(material_buffer_status.map_result)
			<< std::dec << '\n';
		output << "    material_buffer_comparison: resolved="
			<< material_buffer_status.comparisons;
		output << " identical/different=" << material_buffer_status.identical << '/'
			<< material_buffer_status.different;
		output << " unresolved=" << material_buffer_status.unresolved_output0 << '/'
			<< material_buffer_status.unresolved_output1;
		output << " unreferenced_captures="
			<< material_buffer_status.unreferenced_captures;
		output << " count_mismatch="
			<< (material_buffer_status.capture_count_mismatch ? "yes" : "no");
		output << " samples=" << material_buffer_status.sample_count << '\n';
		for (std::size_t index{}; index < material_buffer_status.sample_count; ++index)
		{
			const auto& sample = material_buffer_status.samples[index];
			output << "      material_buffer_draw: family="
				<< dynamic_fx_family_name(static_cast<
					engine_stereo_gpu_census::dynamic_fx_family>(sample.family));
			output << " output_ordinals=" << sample.output_ordinals[0] << '/'
				<< sample.output_ordinals[1];
			output << " family_ordinals=" << sample.output0_family_ordinal << '/'
				<< sample.output1_family_ordinal;
			output << " stage/slot=" << static_cast<std::uint32_t>(sample.stage)
				<< '/' << static_cast<std::uint32_t>(sample.slot);
			output << " buffers=0x" << std::hex << sample.buffer_identities[0]
				<< "/0x" << sample.buffer_identities[1] << std::dec;
			output << " upload_generations=" << sample.upload_generations[0] << '/'
				<< sample.upload_generations[1];
			output << " resolved=" << yes_no(sample.output0_resolved) << '/'
				<< yes_no(sample.output1_resolved);
			output << " capture_ordinals=" << sample.capture_ordinals[0] << '/'
				<< sample.capture_ordinals[1];
			output << " hashes=0x" << std::hex << sample.content_hashes[0]
				<< "/0x" << sample.content_hashes[1] << std::dec;
			output << " bytes(compared/different/first/last)="
				<< sample.compared_bytes << '/' << sample.differing_bytes << '/'
				<< sample.first_difference << '/' << sample.last_difference;
			output << " differences=[";
			for (std::size_t difference{};
				difference < sample.difference_offset_count; ++difference)
			{
				if (difference) output << ',';
				output << sample.difference_offsets[difference] << ":0x" << std::hex
					<< static_cast<std::uint32_t>(sample.output0_values[difference])
					<< "->0x" << static_cast<std::uint32_t>(
						sample.output1_values[difference]) << std::dec;
			}
			output << "]\n";
		}
		output << "  backend_particle_buffer_readback: state="
			<< engine_stereo_particle_buffer_probe::to_string(
				particle_buffer_status.current);
		output << " error=" << engine_stereo_particle_buffer_probe::to_string(
			particle_buffer_status.error);
		output << " pair=" << particle_buffer_status.pair_id;
		output << " generation=" << particle_buffer_status.device_generation;
		output << " context/thread/eye=0x" << std::hex
			<< particle_buffer_status.context << std::dec << '/'
			<< particle_buffer_status.owner_thread << '/'
			<< particle_buffer_status.active_eye;
		output << " completed_eyes=0x" << std::hex
			<< static_cast<std::uint32_t>(
				particle_buffer_status.completed_eye_mask);
		output << " families(observed/complete/incomplete)=0x"
			<< static_cast<std::uint32_t>(
				particle_buffer_status.observed_family_mask) << "/0x"
			<< static_cast<std::uint32_t>(
				particle_buffer_status.complete_family_mask) << "/0x"
			<< static_cast<std::uint32_t>(
				particle_buffer_status.incomplete_family_mask) << std::dec;
		output << " samples=" << particle_buffer_status.sample_count << '\n';
		output << "    particle_buffer_retirement: polls/not_ready="
			<< particle_buffer_status.retirement_polls << '/'
			<< particle_buffer_status.not_ready_polls;
		output << " create(query)=0x" << std::hex << static_cast<std::uint32_t>(
			particle_buffer_status.query_create_result);
		output << " retire(query)=0x" << static_cast<std::uint32_t>(
			particle_buffer_status.query_result);
		output << std::dec << '\n';
		const auto write_particle_comparison = [&output](const char* const name,
			const engine_stereo_particle_buffer_probe::buffer_comparison& value)
		{
			output << "    particle_" << name << ": available/exact/identical="
				<< yes_no(value.available) << '/' << yes_no(value.exact_range) << '/'
				<< yes_no(value.identical);
			output << " resources=0x" << std::hex << value.identities[0]
				<< "/0x" << value.identities[1];
			output << " hashes=0x" << value.hashes[0] << "/0x" << value.hashes[1]
				<< std::dec;
			output << " bytes(source/offset/compared/different/first/last)="
				<< value.source_bytes << '/' << value.compared_offset << '/'
				<< value.compared_bytes << '/' << value.differing_bytes << '/'
				<< value.first_difference << '/' << value.last_difference;
			output << " differences=[";
			for (std::size_t difference{};
				difference < value.difference_offset_count; ++difference)
			{
				if (difference) output << ',';
				output << value.difference_offsets[difference] << ":0x" << std::hex
					<< static_cast<std::uint32_t>(value.output0_values[difference])
					<< "->0x" << static_cast<std::uint32_t>(
						value.output1_values[difference]) << std::dec;
			}
			output << "]\n";
		};
		for (const auto& sample : particle_buffer_status.samples)
		{
			if (sample.family >= engine_stereo_particle_buffer_probe::
				maximum_family_samples) continue;
			const auto family_bit = static_cast<std::uint8_t>(1u << sample.family);
			if ((particle_buffer_status.observed_family_mask & family_bit) == 0 &&
				sample.candidate_hits[0] == 0 && sample.candidate_hits[1] == 0)
			{
				continue;
			}
			output << "    particle_family["
				<< dynamic_fx_family_name(static_cast<
					engine_stereo_gpu_census::dynamic_fx_family>(sample.family))
				<< "]: eyes=0x" << std::hex
				<< static_cast<std::uint32_t>(sample.captured_eye_mask) << std::dec;
			output << " candidate_hits=" << sample.candidate_hits[0] << '/'
				<< sample.candidate_hits[1];
			output << " index_values_equal=" << yes_no(sample.index_values_equal);
			output << " vertex_range_resolved="
				<< yes_no(sample.vertex_range_resolved) << '\n';
			for (std::size_t eye{}; eye < sample.draws.size(); ++eye)
			{
				const auto& draw = sample.draws[eye];
				output << "      particle_draw[" << eye << "]: ordinal="
					<< draw.output_ordinal;
				output << " caller/vs/ps=0x" << std::hex << draw.caller << "/0x"
					<< draw.vertex_shader << "/0x" << draw.pixel_shader;
				output << " ib/vb=0x" << draw.index_buffer << "/0x"
					<< draw.vertex_buffer << std::dec;
				output << " target=" << draw.output_target_id;
				output << " index(count/start/base/format/offset)="
					<< draw.index_count << '/' << draw.start_index << '/'
					<< draw.base_vertex << '/' << draw.index_format << '/'
					<< draw.index_offset;
				output << " vertex(stride/offset)=" << draw.vertex_stride << '/'
					<< draw.vertex_offset << '\n';
			}
			output << "      particle_family_io: create=";
			for (std::size_t index{};
				index < sample.staging_create_results.size(); ++index)
			{
				if (index) output << '/';
				output << "0x" << std::hex << static_cast<std::uint32_t>(
					sample.staging_create_results[index]);
			}
			output << " maps=";
			for (std::size_t index{}; index < sample.map_results.size(); ++index)
			{
				if (index) output << '/';
				output << "0x" << static_cast<std::uint32_t>(
					sample.map_results[index]);
			}
			output << std::dec << '\n';
			write_particle_comparison("index", sample.index);
			output << "      particle_vertex_index_range: min/max="
				<< sample.minimum_index << '/' << sample.maximum_index << '\n';
			write_particle_comparison("vertex", sample.vertex);
			for (std::size_t index{}; index < sample.vertex_sample_count; ++index)
			{
				const auto& vertex = sample.vertices[index];
				output << "      particle_indexed_vertex[" << index << "]: index/offset/bytes="
					<< vertex.index << '/' << vertex.byte_offset << '/' << vertex.captured_bytes
					<< " stride_complete=" << yes_no(vertex.stride_complete)
					<< " cpu_pre_restore(available/left_right_equal)="
					<< yes_no(vertex.cpu_reference_available) << '/' << yes_no(vertex.cpu_eyes_equal)
					<< " gpu_matches_cpu(left/right)=";
				if (vertex.cpu_reference_available)
					output << yes_no(vertex.gpu_matches_cpu_reference[0]) << '/'
						<< yes_no(vertex.gpu_matches_cpu_reference[1]);
				else output << "unknown/unknown";
				const auto write_vertex_bytes = [&](const auto& bytes)
				{
					constexpr char digits[] = "0123456789abcdef";
					for (std::size_t byte{}; byte < vertex.captured_bytes; ++byte)
					{
						if (byte && byte % 4 == 0) output << ' ';
						output << digits[bytes[byte] >> 4] << digits[bytes[byte] & 15];
					}
				};
				output << "\n        gpu0=";
				write_vertex_bytes(vertex.gpu[0]);
				output << "\n        gpu1=";
				write_vertex_bytes(vertex.gpu[1]);
				if (vertex.cpu_reference_available)
				{
					output << "\n        cpu_pre_restore=";
					write_vertex_bytes(vertex.cpu_pre_restore);
				}
				output << '\n';
			}
		}
		output << "  backend_model_eye_state: sync="
			<< owner_pass_status.model_state_sync_matches << '/'
			<< owner_pass_status.model_state_sync_calls;
		output << " foreign=" << owner_pass_status.model_state_foreign_bypasses;
		output << " mismatches=" << owner_pass_status.model_state_contract_mismatches;
		output << " backend_states(reg/reuse/max)="
			<< owner_pass_status.model_state_backend_registrations << '/'
			<< owner_pass_status.model_state_backend_reuses << '/'
			<< owner_pass_status.model_state_backend_maximum_active;
		output << " cache_invalidated=" << owner_pass_status.model_cache_invalidations;
		output << " cache_empty=" << owner_pass_status.model_cache_already_empty;
		output << " last_stage=" << engine_stereo_owner_pass::to_string(
			owner_pass_status.model_state_last_failure_stage);
		output << " last_failure=" << engine_stereo_view::to_string(
			owner_pass_status.model_state_last_failure);
		output << " last_failure_eye=" << owner_pass_status.model_state_last_failure_eye
			<< '\n';
		if (owner_pass_status.model_state_last_failure_stage !=
			engine_stereo_owner_pass::model_state_failure_stage::none)
		{
			output << "  backend_model_eye_state_failure_owner: backend=0x" << std::hex
				<< owner_pass_status.model_state_failure_backend;
			output << "/0x" << owner_pass_status.model_state_failure_expected_backend
				<< std::dec;
			output << " thread=" << owner_pass_status.model_state_failure_thread;
			output << "/" << owner_pass_status.model_state_failure_expected_thread << '\n';
		}
		if (owner_pass_status.model_state_last_failure !=
			engine_stereo_view::backend_model_state_sync_failure::none)
		{
			output << "  backend_model_eye_state_failure_sources: primary=0x" << std::hex
				<< owner_pass_status.model_state_failure_primary_source;
			output << "/0x"
				<< owner_pass_status.model_state_failure_expected_primary_source;
			output << " rebase=0x" << owner_pass_status.model_state_failure_rebase_source;
			output << "/0x" << owner_pass_status.model_state_failure_expected_rebase_source
				<< std::dec << '\n';
			output << "  backend_model_eye_state_failure_values: primary=";
			for (std::size_t component{}; component < 3; ++component)
			{
				if (component != 0) output << ',';
				output << owner_pass_status.model_state_failure_primary_origin[component];
			}
			output << "/";
			for (std::size_t component{}; component < 3; ++component)
			{
				if (component != 0) output << ',';
				output << owner_pass_status.model_state_failure_expected_primary_origin[component];
			}
			output << " relative=";
			for (std::size_t component{}; component < 3; ++component)
			{
				if (component != 0) output << ',';
				output << owner_pass_status.model_state_failure_relative_eye_offset[component];
			}
			output << "/";
			for (std::size_t component{}; component < 3; ++component)
			{
				if (component != 0) output << ',';
				output << owner_pass_status.model_state_failure_expected_relative_eye_offset[
					component];
			}
			output << '\n';
		}
		output << "  backend_depth_hack_projection: restores="
			<< owner_pass_status.depth_hack_projection_restores << '/'
			<< owner_pass_status.depth_hack_projection_calls;
		output << " bypass=" << owner_pass_status.depth_hack_projection_bypasses;
		output << " foreign="
			<< owner_pass_status.depth_hack_projection_foreign_bypasses;
		output << " mismatches="
			<< owner_pass_status.depth_hack_projection_contract_mismatches << '\n';
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			output << "  backend_model_eye[" << eye << "]: matches="
				<< owner_pass_status.model_state_eye_matches[eye];
			output << " cache_invalidations="
				<< owner_pass_status.model_cache_eye_invalidations[eye];
			output << " cache_last=0x" << std::hex
				<< owner_pass_status.model_cache_last_values[eye] << std::dec;
			output << " primary=" << owner_pass_status.model_primary_origins[eye][0]
				<< ',' << owner_pass_status.model_primary_origins[eye][1] << ','
				<< owner_pass_status.model_primary_origins[eye][2];
			output << " relative=" << owner_pass_status.model_relative_eye_offsets[eye][0]
				<< ',' << owner_pass_status.model_relative_eye_offsets[eye][1] << ','
				<< owner_pass_status.model_relative_eye_offsets[eye][2] << '\n';
			output << "  backend_depth_hack_projection[" << eye << "]: restores="
				<< owner_pass_status.depth_hack_projection_eye_restores[eye];
			output << " previous_m00_m11_m20_m21=";
			for (std::size_t term{}; term < 4; ++term)
			{
				if (term != 0) output << ',';
				output << owner_pass_status.depth_hack_projection_previous_terms[eye][term];
			}
			output << " restored_m00_m11_m20_m21=";
			for (std::size_t term{}; term < 4; ++term)
			{
				if (term != 0) output << ',';
				output << owner_pass_status.depth_hack_projection_restored_terms[eye][term];
			}
			output << " near=" << owner_pass_status.depth_hack_projection_near[eye]
				<< '\n';
			output << "  backend_model_lists_bit_exact[" << eye << "]: pair="
				<< owner_pass_status.model_list_pair_id << " samples=68 eye="
				<< owner_pass_status.model_list_eye_origin_matches[eye];
			output << " center="
				<< owner_pass_status.model_list_center_origin_matches[eye];
			output << " zero=" << owner_pass_status.model_list_zero_origins[eye];
			output << " other=" << owner_pass_status.model_list_other_origins[eye] << '\n';
			output << "  backend_camera_model_lists[" << eye << "]: boundary_rewrites="
				<< owner_pass_status.camera_model_boundary_rewrites[eye];
			output << " boundary_foreign="
				<< owner_pass_status.camera_model_boundary_foreign[eye];
			output << " final_eye=" << owner_pass_status.camera_model_final_eye[eye];
			output << " final_inactive="
				<< owner_pass_status.camera_model_final_inactive[eye];
			output << " final_other=" << owner_pass_status.camera_model_final_other[eye]
				<< '\n';
		}
	}
}
