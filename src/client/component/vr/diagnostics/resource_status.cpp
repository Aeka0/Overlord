#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "../engine_stereo_constant_buffer_probe.hpp"
#include "../engine_stereo_gpu_census.hpp"
#include "../engine_stereo_resource_ops.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr::diagnostics::detail
{
	void append_resource_status(std::ostringstream& output,
		const engine_stereo_gpu_census::report& gpu_census_status,
		const engine_stereo_constant_buffer_probe::report& constant_buffer_status)
	{
		const auto& resource_hooks = gpu_census_status.resource_operation_hooks;
		output << "  backend_gpu_census_resource_ops: hooks="
			<< (resource_hooks.hooks_installed ? "yes" : "no");
		output << " context=0x" << std::hex << resource_hooks.expected_context
			<< std::dec;
		output << " generation=" << resource_hooks.device_generation;
		output << " failures=" << resource_hooks.hook_failures;
		output << " callbacks=" << resource_hooks.observer_callbacks;
		output << " foreign_context=" << resource_hooks.foreign_context_calls;
		for (std::size_t operation{};
			operation < engine_stereo_resource_ops::api_count; ++operation)
		{
			output << ' ' << engine_stereo_resource_ops::to_string(
				static_cast<engine_stereo_resource_ops::api>(operation));
			output << '=' << resource_hooks.calls[operation];
		}
		output << '\n';
		const auto constant_buffer_strict_evidence =
			constant_buffer_status.current_state ==
				engine_stereo_constant_buffer_probe::state::complete &&
			constant_buffer_status.selected_resource_history_complete &&
			constant_buffer_status.hook_failures == 0 &&
			constant_buffer_status.callback_quiescence_timeouts == 0 &&
			constant_buffer_status.candidate_registration_overflows == 0 &&
			constant_buffer_status.unregistered_context_calls == 0 &&
			constant_buffer_status.foreign_context_calls == 0 &&
			constant_buffer_status.foreign_thread_pair_calls == 0 &&
			constant_buffer_status.unmatched_unmaps == 0 &&
			constant_buffer_status.pending_map_overflows == 0 &&
			constant_buffer_status.tracked_buffer_overflows == 0 &&
			constant_buffer_status.tracked_shader_overflows == 0 &&
			constant_buffer_status.buffer_identity_tag_failures == 0 &&
			constant_buffer_status.shader_identity_tag_failures == 0 &&
			constant_buffer_status.concurrent_update_drops == 0 &&
			constant_buffer_status.unstable_content_reads == 0 &&
			constant_buffer_status.shader_private_data_missing == 0 &&
			constant_buffer_status.shader_private_data_oversized == 0 &&
			constant_buffer_status.shader_reflection_failures == 0 &&
			constant_buffer_status.resource_unknown_writes == 0 &&
			constant_buffer_status.unknown_usage_left_bound_right_null == 0 &&
			constant_buffer_status.both_bound_content_unknown == 0 &&
			constant_buffer_status.provenance_unknown == 0 &&
			constant_buffer_status.ordinal_mismatches == 0 &&
			constant_buffer_status.draw_overflows == 0 &&
			constant_buffer_status.eyes[0].set_event_overflows == 0 &&
			constant_buffer_status.eyes[1].set_event_overflows == 0;
		output << "  backend_vs_cb3_probe: capture_state="
			<< engine_stereo_constant_buffer_probe::to_string(
				constant_buffer_status.current_state);
		output << " strict_evidence="
			<< (constant_buffer_strict_evidence ? "complete" : "incomplete");
		output << " hooks=" << (constant_buffer_status.hooks_installed ? "yes" : "no");
		output << " history_tracking="
			<< (constant_buffer_status.history_tracking_enabled ? "enabled" : "idle");
		output << " tracking_clients=0x" << std::hex
			<< constant_buffer_status.history_tracking_clients << std::dec;
		output << " device/context=0x" << std::hex
			<< constant_buffer_status.expected_device << "/0x"
			<< constant_buffer_status.expected_context << std::dec;
		output << " generation=" << constant_buffer_status.device_generation;
		output << " candidates=" << constant_buffer_status.registered_device_candidates;
		output << " candidate_overflows="
			<< constant_buffer_status.candidate_registration_overflows;
		output << " resource_history_complete="
			<< (constant_buffer_status.selected_resource_history_complete ? "yes" : "no");
		output << " pair=" << constant_buffer_status.pair_id;
		output << " eyes=0x" << std::hex
			<< static_cast<std::uint32_t>(constant_buffer_status.completed_eye_mask)
			<< std::dec;
		output << " owner_thread=" << constant_buffer_status.owner_thread;
		output << " hook_failures=" << constant_buffer_status.hook_failures;
		output << " quiescence_timeouts="
			<< constant_buffer_status.callback_quiescence_timeouts << '\n';
		output << "    vs_cb3_hooks: set_targets=";
		for (std::size_t stage{}; stage < constant_buffer_status.set_hook_targets.size();
			++stage)
		{
			if (stage) output << ',';
			output << "0x" << std::hex
				<< constant_buffer_status.set_hook_targets[stage] << std::dec;
		}
		output << " map/unmap=0x" << std::hex
			<< constant_buffer_status.map_hook_target << "/0x"
			<< constant_buffer_status.unmap_hook_target << std::dec;
		output << " set_calls(vs/ps/gs/hs/ds/cs)=";
		for (std::size_t stage{}; stage < constant_buffer_status.set_calls.size(); ++stage)
		{
			if (stage) output << '/';
			output << constant_buffer_status.set_calls[stage];
		}
		output << '\n';
		output << "    vs_cb3_tracking: map/unmap/uploads/unmatched="
			<< constant_buffer_status.map_calls << '/'
			<< constant_buffer_status.unmap_calls << '/'
			<< constant_buffer_status.mapped_uploads << '/'
			<< constant_buffer_status.unmatched_unmaps;
		output << " resource(update/copy/unknown)="
			<< constant_buffer_status.resource_updates << '/'
			<< constant_buffer_status.resource_copies << '/'
			<< constant_buffer_status.resource_unknown_writes;
		output << " copy_source(candidate/registered/known/bytes)="
			<< constant_buffer_status.copy_source_candidates << '/'
			<< constant_buffer_status.copy_source_registered << '/'
			<< constant_buffer_status.copy_source_known << '/'
			<< constant_buffer_status.copy_source_bytes_propagated;
		output << " copy_source_samples="
			<< constant_buffer_status.copy_source_sample_count << '/'
			<< constant_buffer_status.copy_source_sample_overflows;
		output << " overflow(map/buffer/shader/draw)="
			<< constant_buffer_status.pending_map_overflows << '/'
			<< constant_buffer_status.tracked_buffer_overflows << '/'
			<< constant_buffer_status.tracked_shader_overflows << '/'
			<< constant_buffer_status.draw_overflows;
		output << " identity_tag_fail(buffer/shader)="
			<< constant_buffer_status.buffer_identity_tag_failures << '/'
			<< constant_buffer_status.shader_identity_tag_failures;
		output << " identity_reuse(buffer/shader)="
			<< constant_buffer_status.buffer_identity_reuses << '/'
			<< constant_buffer_status.shader_identity_reuses;
		output << " update_drops=" << constant_buffer_status.concurrent_update_drops;
		output << " unstable_reads=" << constant_buffer_status.unstable_content_reads;
		output << " unregistered_context="
			<< constant_buffer_status.unregistered_context_calls;
		output << " foreign(context/thread)="
			<< constant_buffer_status.foreign_context_calls << '/'
			<< constant_buffer_status.foreign_thread_pair_calls << '\n';
		for (std::size_t index{};
			index < constant_buffer_status.copy_source_sample_count; ++index)
		{
			const auto& sample = constant_buffer_status.copy_source_samples[index];
			output << "      vs_cb3_copy_source_sample[" << index << "]: dst=0x"
				<< std::hex << sample.destination << ':' << std::dec
				<< sample.destination_creation_serial << '/' << sample.destination_byte_width;
			output << " src=0x" << std::hex << sample.source << ':' << std::dec
				<< sample.source_creation_serial;
			output << " generation=" << sample.device_generation;
			output << " observations(registered/known/bytes)=" << sample.observations
				<< '(' << sample.registered_observations << '/'
				<< sample.known_observations << '/'
				<< sample.byte_snapshot_observations << ')';
			output << " source_desc(dimension/bytes/usage/bind/cpu/misc/stride)="
				<< sample.source_dimension << '/' << sample.source_byte_width << '/'
				<< sample.source_usage << "/0x" << std::hex
				<< sample.source_bind_flags << "/0x" << sample.source_cpu_access_flags
				<< "/0x" << sample.source_misc_flags << std::dec << '/'
				<< sample.source_structure_stride;
			output << " failure(null/nonbuffer/size/register/unknown/bytes)="
				<< sample.null_source_observations << '/'
				<< sample.non_buffer_observations << '/'
				<< sample.size_mismatch_observations << '/'
				<< sample.registration_failure_observations << '/'
				<< sample.unknown_content_observations << '/'
				<< sample.byte_snapshot_failure_observations;
			output << " source_content(generation/origin/caller/thread)="
				<< sample.source_upload_generation << '/'
				<< engine_stereo_constant_buffer_probe::to_string(
					sample.source_content_origin) << "/0x" << std::hex
				<< sample.source_upload_caller << std::dec << '/'
				<< sample.source_upload_thread;
			output << " copy(caller/thread)=0x" << std::hex << sample.copy_caller
				<< std::dec << '/' << sample.copy_thread << '\n';
		}
		output << "    vs_cb3_shader_classification: private_missing="
			<< constant_buffer_status.shader_private_data_missing;
		output << " oversized=" << constant_buffer_status.shader_private_data_oversized;
		output << " reflection_failures="
			<< constant_buffer_status.shader_reflection_failures << '\n';
		output << "    vs_cb3_comparison: comparisons="
			<< constant_buffer_status.comparisons;
		output << " left_bound_right_null(used/unused/unknown)="
			<< constant_buffer_status.used_left_bound_right_null << '/'
			<< constant_buffer_status.unused_left_bound_right_null << '/'
			<< constant_buffer_status.unknown_usage_left_bound_right_null;
		output << " both_bound(same/different/unknown)="
			<< constant_buffer_status.both_bound_same_content << '/'
			<< constant_buffer_status.both_bound_different_content << '/'
			<< constant_buffer_status.both_bound_content_unknown;
		output << " same_identity_new_generation="
			<< constant_buffer_status.same_identity_new_generation;
		output << " provenance_unknown=" << constant_buffer_status.provenance_unknown;
		output << " ordinal_mismatches=" << constant_buffer_status.ordinal_mismatches
			<< '\n';
		const auto emit_cb3_slot = [&](const char* const name,
			const engine_stereo_constant_buffer_probe::slot_snapshot& slot)
		{
			output << "      " << name << ": shader=0x" << std::hex << slot.shader
				<< std::dec;
			output << " shader_generation=" << slot.shader_device_generation;
			output << " usage="
				<< engine_stereo_constant_buffer_probe::to_string(slot.usage);
			output << " buffer=0x" << std::hex << slot.content.buffer << std::dec;
			output << " buffer_generation=" << slot.content.device_generation;
			output << " serial=" << slot.content.creation_serial;
			output << " upload_generation=" << slot.content.upload_generation;
			output << " bytes=" << slot.content.byte_width;
			output << " content_known=" << (slot.content.known ? "yes" : "no");
			output << " hash=0x" << std::hex << slot.content.hash_low << "/0x"
				<< slot.content.hash_high << std::dec;
			output << " upload="
				<< engine_stereo_constant_buffer_probe::to_string(slot.content.source);
			output << " upload_caller=0x" << std::hex << slot.content.upload_caller
				<< std::dec;
			output << " upload_thread=" << slot.content.upload_thread;
			output << " bind="
				<< engine_stereo_constant_buffer_probe::to_string(slot.origin);
			output << " set_sequence=" << slot.last_set_sequence;
			output << " set_caller=0x" << std::hex << slot.last_set_caller
				<< std::dec;
			output << " set_thread=" << slot.last_set_thread << '\n';
		};
		for (std::size_t eye{}; eye < constant_buffer_status.eyes.size(); ++eye)
		{
			const auto& cb_eye = constant_buffer_status.eyes[eye];
			output << "    vs_cb3_eye[" << eye << "]: draws=" << cb_eye.draws;
			output << " shader(used/unused/unknown)=" << cb_eye.shader_used_draws << '/'
				<< cb_eye.shader_unused_draws << '/' << cb_eye.shader_unknown_draws;
			output << " setters=" << cb_eye.setter_touches;
			output << " bind/null/clear/opaque=" << cb_eye.explicit_binds << '/'
				<< cb_eye.explicit_nulls << '/' << cb_eye.clear_states << '/'
				<< cb_eye.opaque_state_changes;
			output << " set_events=" << cb_eye.set_event_count;
			output << " event_overflows=" << cb_eye.set_event_overflows << '\n';
			emit_cb3_slot("begin", cb_eye.begin);
			emit_cb3_slot("end", cb_eye.end);
			const auto event_limit = (std::min)(std::size_t{16}, cb_eye.set_event_count);
			for (std::size_t index{}; index < event_limit; ++index)
			{
				const auto& event = cb_eye.set_events[index];
				output << "      vs_cb3_set[" << eye << "][" << index
					<< "]: sequence=" << event.sequence;
				output << " origin="
					<< engine_stereo_constant_buffer_probe::to_string(event.origin);
				output << " buffer=0x" << std::hex << event.buffer;
				output << " caller=0x" << event.caller << std::dec;
				output << " thread=" << event.thread << '\n';
			}
		}
		for (std::size_t index{}; index < constant_buffer_status.sample_count; ++index)
		{
			const auto& sample = constant_buffer_status.samples[index];
			output << "    vs_cb3_sample[" << index << "]: ordinal=" << sample.ordinal;
			output << " callers(left/right)=0x" << std::hex << sample.left_caller
				<< "/0x" << sample.right_caller << std::dec << '\n';
			emit_cb3_slot("left", sample.left);
			emit_cb3_slot("right", sample.right);
		}

		const auto shared_identity_count = [](const auto& left,
			const std::size_t left_count, const auto& right,
			const std::size_t right_count)
		{
			std::size_t shared{};
			for (std::size_t left_index{}; left_index < left_count; ++left_index)
			{
				for (std::size_t right_index{}; right_index < right_count; ++right_index)
				{
					if (left[left_index].identity == right[right_index].identity)
					{
						++shared;
						break;
					}
				}
			}
			return shared;
		};
		for (std::size_t eye{}; eye < gpu_census_status.eyes.size(); ++eye)
		{
			const auto& census_eye = gpu_census_status.eyes[eye];
			output << "  backend_gpu_census_eye[" << eye << "]: draws="
				<< census_eye.draw_calls;
			output << " dispatch=" << census_eye.dispatch_calls;
			output << " observations=" << census_eye.observations;
			output << " shaders_unique(vs/ps/cs)=" << census_eye.vs_shader_count
				<< '/' << census_eye.ps_shader_count << '/' << census_eye.cs_shader_count;
			output << " cb_identities=" << census_eye.constant_buffer_count;
			output << " depth_ranges=" << census_eye.depth_range_count;
			output << " clears(rtv/dsv/depth/stencil)="
				<< census_eye.clear_render_target_calls << '/'
				<< census_eye.clear_depth_stencil_calls << '/'
				<< census_eye.clear_depth_calls << '/'
				<< census_eye.clear_stencil_calls;
			output << " states(depth/blend/raster)=" << census_eye.depth_state_count
				<< '/' << census_eye.blend_state_count << '/'
				<< census_eye.rasterizer_state_count;
			output << " resource_ops=";
			for (std::size_t operation{};
				operation < engine_stereo_resource_ops::api_count; ++operation)
			{
				if (operation) output << ',';
				output << engine_stereo_resource_ops::to_string(
					static_cast<engine_stereo_resource_ops::api>(operation));
				output << ':' << census_eye.resource_operations[operation];
			}
			output << '\n';
			output << "    gpu_census_consumption[" << eye << "]: srv_seen=";
			for (std::size_t stage{}; stage < census_eye.srv_bindings_seen.size(); ++stage)
			{
				if (stage) output << ',';
				output << census_eye.srv_bindings_seen[stage];
			}
			output << " srv_admitted=";
			for (std::size_t stage{}; stage < census_eye.srv_bindings_admitted.size();
				++stage)
			{
				if (stage) output << ',';
				output << census_eye.srv_bindings_admitted[stage];
			}
			output << " srv_ignored=";
			for (std::size_t stage{}; stage < census_eye.srv_bindings_ignored.size();
				++stage)
			{
				if (stage) output << ',';
				output << census_eye.srv_bindings_ignored[stage];
			}
			output << " stage_order=vs,ps,cs,gs,hs,ds";
			output << " om_rtv=" << census_eye.om_rtv_bindings_seen << '/'
				<< census_eye.om_rtv_bindings_admitted << '/'
				<< census_eye.om_rtv_bindings_ignored;
			output << " om_dsv=" << census_eye.om_dsv_bindings_seen << '/'
				<< census_eye.om_dsv_bindings_admitted << '/'
				<< census_eye.om_dsv_bindings_ignored;
			output << " om_uav=" << census_eye.om_uav_bindings_seen << '/'
				<< census_eye.om_uav_bindings_admitted << '/'
				<< census_eye.om_uav_bindings_ignored;
			output << " cs_uav=" << census_eye.cs_uav_bindings_seen << '/'
				<< census_eye.cs_uav_bindings_admitted << '/'
				<< census_eye.cs_uav_bindings_ignored << '\n';
			const auto depth_limit = (std::min)(std::size_t{8},
				census_eye.depth_range_count);
			for (std::size_t index{}; index < depth_limit; ++index)
			{
				const auto& range = census_eye.depth_ranges[index];
				output << "    gpu_census_depth[" << eye << "][" << index << "]="
					<< float_from_bits(range.min_depth_bits) << ".."
					<< float_from_bits(range.max_depth_bits);
				output << " viewports=" << range.viewports << '\n';
			}
		}
		const auto& census_left = gpu_census_status.eyes[0];
		const auto& census_right = gpu_census_status.eyes[1];
		output << "  backend_gpu_census_shader_identity_shared(vs/ps/cs)="
			<< shared_identity_count(census_left.vs_shaders,
				census_left.vs_shader_count, census_right.vs_shaders,
				census_right.vs_shader_count) << '/'
			<< shared_identity_count(census_left.ps_shaders,
				census_left.ps_shader_count, census_right.ps_shaders,
				census_right.ps_shader_count) << '/'
			<< shared_identity_count(census_left.cs_shaders,
				census_left.cs_shader_count, census_right.cs_shaders,
				census_right.cs_shader_count) << '\n';
		output << "  backend_gpu_census_state_identity_shared(depth/blend/raster)="
			<< shared_identity_count(census_left.depth_states,
				census_left.depth_state_count, census_right.depth_states,
				census_right.depth_state_count) << '/'
			<< shared_identity_count(census_left.blend_states,
				census_left.blend_state_count, census_right.blend_states,
				census_right.blend_state_count) << '/'
			<< shared_identity_count(census_left.rasterizer_states,
				census_left.rasterizer_state_count, census_right.rasterizer_states,
				census_right.rasterizer_state_count) << '\n';
		const auto& ordered = gpu_census_status.ordered;
		output << "  backend_gpu_census_ordered: comparisons=" << ordered.comparisons;
		output << " mismatches=" << ordered.mismatches;
		output << " categories(api/caller/args/program/target/rtv/dsv/depth/blend/layout/blend_factor/raster/topology/index/viewport/scissor/cb/srv/uav/outputs)="
			<< ordered.api_mismatches << '/' << ordered.caller_mismatches << '/'
			<< ordered.argument_mismatches << '/' << ordered.program_mismatches << '/'
			<< ordered.output_target_mismatches << '/'
			<< ordered.render_target_mismatches << '/'
			<< ordered.depth_target_mismatches << '/'
			<< ordered.depth_state_mismatches << '/'
			<< ordered.blend_state_mismatches << '/'
			<< ordered.input_layout_mismatches << '/'
			<< ordered.blend_factor_mismatches << '/'
			<< ordered.rasterizer_state_mismatches << '/'
			<< ordered.topology_mismatches << '/'
			<< ordered.index_buffer_mismatches << '/'
			<< ordered.viewport_mismatches << '/'
			<< ordered.scissor_mismatches << '/'
			<< ordered.constant_buffer_mismatches << '/'
			<< ordered.shader_resource_mismatches << '/'
			<< ordered.unordered_access_mismatches << '/'
			<< ordered.output_binding_mismatches;
		output << " first_ordinal=" << ordered.first_mismatch_ordinal;
		output << " first_mask=0x" << std::hex << ordered.first_mismatch_mask
			<< std::dec << '\n';
		output << "  backend_gpu_census_detail_samples(args/cb/srv)="
			<< ordered.argument_sample_count << '/'
			<< ordered.constant_buffer_sample_count << '/'
			<< ordered.shader_resource_sample_count;
		output << " snapshot_truncations(cb/srv)="
			<< ordered.constant_buffer_snapshot_truncations << '/'
			<< ordered.shader_resource_snapshot_truncations;
		output << " bindings_dropped(cb/srv)="
			<< ordered.constant_buffer_bindings_dropped << '/'
			<< ordered.shader_resource_bindings_dropped;
		output << " static_storage_bytes=" << ordered.snapshot_static_storage_bytes
			<< '\n';
		const auto emit_arguments = [&](
			const engine_stereo_gpu_census::invocation_arguments& arguments)
		{
			output << "count=" << static_cast<std::uint32_t>(arguments.count);
			output << " values=[";
			for (std::uint8_t index{}; index < arguments.count &&
				index < arguments.values.size(); ++index)
			{
				if (index) output << ',';
				output << arguments.values[index];
			}
			output << ']';
		};
		if (ordered.first_mismatch_ordinal)
		{
			const auto emit_signature = [&](const char* const eye,
				const engine_stereo_gpu_census::observation_signature& value)
			{
				output << "    gpu_census_first_ordered_" << eye;
				output << ": api=" << static_cast<std::uint32_t>(value.operation);
				output << " caller=0x" << std::hex << value.caller;
				output << " args_hash=0x" << value.argument_hash << std::dec << ' ';
				emit_arguments(value.arguments);
				output << std::hex;
				output << " program(vs/ps/cs/gs/hs/ds)=0x" << value.program.vs
					<< "/0x" << value.program.ps << "/0x" << value.program.cs
					<< "/0x" << value.program.gs << "/0x" << value.program.hs
					<< "/0x" << value.program.ds << std::dec;
				output << " target=" << value.output_target_id;
				output << " rtv/dsv=0x" << std::hex
					<< value.pipeline.render_target_resource << "/0x"
					<< value.pipeline.depth_stencil_resource;
				output << " states=0x" << value.pipeline.depth_stencil_state
					<< "/0x" << value.pipeline.blend_state << "/0x"
					<< value.pipeline.rasterizer_state << "/0x"
					<< value.pipeline.input_layout;
				output << " blend_factor_bits=" << value.pipeline.blend_factor_bits[0]
					<< '/' << value.pipeline.blend_factor_bits[1] << '/'
					<< value.pipeline.blend_factor_bits[2] << '/'
					<< value.pipeline.blend_factor_bits[3];
				output << " viewport(count/hash)=" << std::dec
					<< value.pipeline.viewport_count << "/0x" << std::hex
					<< value.pipeline.viewport_hash;
				output << " scissor(count/hash)=" << std::dec
					<< value.pipeline.scissor_count << "/0x" << std::hex
					<< value.pipeline.scissor_hash;
				output << " index=0x" << value.pipeline.index_buffer << std::dec;
				output << " topology=" << value.pipeline.primitive_topology;
				output << " binding_hashes(cb/srv/uav/out)=0x" << std::hex
					<< value.bindings.constant_buffers << "/0x"
					<< value.bindings.shader_resources << "/0x"
					<< value.bindings.unordered_access << "/0x"
					<< value.bindings.outputs << std::dec;
				output << " binding_counts=" << value.bindings.constant_buffer_count
					<< '/' << value.bindings.shader_resource_count << '/'
					<< value.bindings.unordered_access_count << '/'
					<< value.bindings.output_count << '\n';
			};
			emit_signature("left", ordered.first_left);
			emit_signature("right", ordered.first_right);
		}
		for (std::size_t index{}; index < ordered.argument_sample_count; ++index)
		{
			const auto& sample = ordered.argument_samples[index];
			output << "    gpu_census_args_sample[" << index << "]: ordinal="
				<< sample.ordinal;
			output << " api=" << static_cast<std::uint32_t>(sample.left.operation)
				<< '/' << static_cast<std::uint32_t>(sample.right.operation);
			output << " caller=0x" << std::hex << sample.left.caller << "/0x"
				<< sample.right.caller << std::dec;
			output << " target=" << sample.left.output_target_id << '/'
				<< sample.right.output_target_id << " left_";
			emit_arguments(sample.left.arguments);
			output << " right_";
			emit_arguments(sample.right.arguments);
			output << '\n';
		}
		const auto ordered_stage_name = [](const std::uint8_t stage)
		{
			switch (stage)
			{
			case 0: return "vs";
			case 1: return "ps";
			case 2: return "cs";
			case 3: return "gs";
			case 4: return "hs";
			case 5: return "ds";
			default: return "unknown";
			}
		};
		const auto srv_usage_name = [](
			const engine_stereo_gpu_census::srv_shader_usage usage)
		{
			switch (usage)
			{
			case engine_stereo_gpu_census::srv_shader_usage::used: return "used";
			case engine_stereo_gpu_census::srv_shader_usage::unused: return "unused";
			default: return "unknown";
			}
		};
		const auto& srv_usage = ordered.shader_resource_usage;
		output << "  backend_gpu_census_srv_usage: classification_complete="
			<< (srv_usage.classification_complete ? "yes" : "no");
		output << " slot_mismatches=" << srv_usage.slot_mismatches;
		output << " used/unused/unknown=" << srv_usage.used << '/'
			<< srv_usage.unused << '/' << srv_usage.unknown;
		output << " descriptor_same/different/unknown="
			<< srv_usage.descriptor_same << '/' << srv_usage.descriptor_different
			<< '/' << srv_usage.descriptor_unknown;
		output << " reflected(shaders/bindings)=" << srv_usage.reflected_shaders
			<< '/' << srv_usage.reflected_bindings;
		output << " cache_overflow(shader/descriptor)="
			<< srv_usage.shader_cache_overflows << '/'
			<< srv_usage.descriptor_cache_overflows;
		output << " bytecode(missing/oversized)="
			<< srv_usage.shader_bytecode_missing << '/'
			<< srv_usage.shader_bytecode_oversized;
		output << " reflection_failures=" << srv_usage.shader_reflection_failures;
		output << " stage_mismatches=" << srv_usage.shader_stage_mismatches;
		output << " binding_overflows=" << srv_usage.shader_binding_overflows;
		output << " sample_overflows=" << srv_usage.sample_overflows;
		output << " debug_name(missing/truncated)="
			<< srv_usage.shader_debug_name_missing << '/'
			<< srv_usage.shader_debug_name_truncations;
		output << " binding_name_truncated=" << srv_usage.binding_name_truncations;
		output << " cache_miss(shader/descriptor)="
			<< srv_usage.shader_cache_misses << '/'
			<< srv_usage.descriptor_cache_misses << '\n';
		for (std::size_t stage{}; stage < srv_usage.slots.size(); ++stage)
		{
			for (std::size_t slot{}; slot < srv_usage.slots[stage].size(); ++slot)
			{
				const auto& value = srv_usage.slots[stage][slot];
				if (!value.mismatches) continue;
				output << "    gpu_census_srv_usage_slot: stage="
					<< ordered_stage_name(static_cast<std::uint8_t>(stage));
				output << " slot=" << slot << " mismatches=" << value.mismatches;
				output << " used/unused/unknown=" << value.used << '/'
					<< value.unused << '/' << value.unknown;
				output << " left(used/unused/unknown)=" << value.left_used << '/'
					<< value.left_unused << '/' << value.left_unknown;
				output << " right(used/unused/unknown)=" << value.right_used << '/'
					<< value.right_unused << '/' << value.right_unknown;
				output << " descriptor_same/different/unknown="
					<< value.descriptor_same << '/' << value.descriptor_different
					<< '/' << value.descriptor_unknown << '\n';
			}
		}
		for (std::size_t index{}; index < ordered.constant_buffer_sample_count; ++index)
		{
			const auto& sample = ordered.constant_buffer_samples[index];
			output << "    gpu_census_cb_slot_sample[" << index << "]: ordinal="
				<< sample.ordinal;
			output << " api=" << static_cast<std::uint32_t>(sample.left.operation)
				<< '/' << static_cast<std::uint32_t>(sample.right.operation);
			output << " caller=0x" << std::hex << sample.left.caller << "/0x"
				<< sample.right.caller << std::dec;
			output << " target=" << sample.left.output_target_id << '/'
				<< sample.right.output_target_id;
			output << " stage=" << ordered_stage_name(sample.stage);
			output << " slot=" << static_cast<std::uint32_t>(sample.slot);
			output << " identity=0x" << std::hex << sample.left_identity << "/0x"
				<< sample.right_identity << std::dec << '\n';
		}
		const auto emit_srv_binding = [&](const char* const eye,
			const engine_stereo_gpu_census::srv_binding_identity& binding)
		{
			output << ' ' << eye << "_view/resource=0x" << std::hex << binding.view
				<< "/0x" << binding.resource << std::dec;
			output << " format/dimension=" << binding.range.format << '/'
				<< binding.range.dimension;
			output << " mip=" << binding.range.most_detailed_mip << '+'
				<< binding.range.mip_levels;
			output << " array=" << binding.range.first_array_slice << '+'
				<< binding.range.array_size;
			output << " elements=" << binding.range.first_element << '+'
				<< binding.range.element_count;
		};
		const auto emit_srv_shader = [&](const char* const eye,
			const engine_stereo_gpu_census::shader_resource_mismatch_sample::
				shader_binding_classification& shader)
		{
			output << ' ' << eye << "_shader=0x" << std::hex << shader.shader
				<< std::dec;
			output << " usage=" << srv_usage_name(shader.usage);
			output << " debug_name=\"" << shader.shader_debug_name.data() << "\"";
			output << " debug_hash=0x" << std::hex << shader.shader_debug_name_hash
				<< std::dec;
			output << " binding_name=\"" << shader.binding_name.data() << "\"";
			output << " binding_hash=0x" << std::hex << shader.binding_name_hash
				<< std::dec;
			output << " type/return/dimension=" << shader.input_type << '/'
				<< shader.return_type << '/' << shader.dimension;
			output << " bind=" << shader.bind_point << '+' << shader.bind_count;
		};
		const auto emit_srv_resource = [&](const char* const eye,
			const engine_stereo_gpu_census::resource_descriptor& descriptor)
		{
			output << ' ' << eye << "_resource_meta="
				<< (descriptor.captured ? "captured" : "missing") << '/'
				<< (descriptor.valid ? "valid" : "invalid");
			output << " dimension=" << descriptor.dimension;
			output << " size=" << descriptor.width << 'x' << descriptor.height
				<< 'x' << descriptor.depth;
			output << " mip/array=" << descriptor.mip_levels << '/'
				<< descriptor.array_size;
			output << " format=" << descriptor.format;
			output << " samples=" << descriptor.sample_count << '+'
				<< descriptor.sample_quality;
			output << " usage/bind/cpu/misc=" << descriptor.usage << "/0x"
				<< std::hex << descriptor.bind_flags << "/0x"
				<< descriptor.cpu_access_flags << "/0x" << descriptor.misc_flags
				<< std::dec;
			output << " bytes/stride=" << descriptor.byte_width << '/'
				<< descriptor.structure_byte_stride;
		};
		for (std::size_t index{}; index < ordered.shader_resource_sample_count; ++index)
		{
			const auto& sample = ordered.shader_resource_samples[index];
			output << "    gpu_census_srv_slot_sample[" << index << "]: ordinal="
				<< sample.ordinal;
			output << " api=" << static_cast<std::uint32_t>(sample.left.operation)
				<< '/' << static_cast<std::uint32_t>(sample.right.operation);
			output << " caller=0x" << std::hex << sample.left.caller << "/0x"
				<< sample.right.caller << std::dec;
			output << " target=" << sample.left.output_target_id << '/'
				<< sample.right.output_target_id;
			output << " left_";
			emit_arguments(sample.left_arguments);
			output << " right_";
			emit_arguments(sample.right_arguments);
			output << " stage=" << ordered_stage_name(sample.stage);
			output << " slot=" << static_cast<std::uint32_t>(sample.slot);
			emit_srv_binding("left", sample.left_binding);
			emit_srv_binding("right", sample.right_binding);
			emit_srv_shader("left", sample.left_shader);
			emit_srv_shader("right", sample.right_shader);
			emit_srv_resource("left", sample.left_resource);
			emit_srv_resource("right", sample.right_resource);
			output << '\n';
		}

		const auto binding_present = [](const engine_stereo_gpu_census::eye_report& eye,
			const engine_stereo_gpu_census::binding_count& candidate)
		{
			for (std::size_t index{}; index < eye.constant_buffer_count; ++index)
			{
				const auto& value = eye.constant_buffers[index];
				if (value.stage == candidate.stage && value.slot == candidate.slot &&
					value.identity == candidate.identity) return true;
			}
			return false;
		};
		std::size_t shared_bindings{};
		std::size_t left_only_bindings{};
		std::size_t right_only_bindings{};
		for (std::size_t index{}; index < census_left.constant_buffer_count; ++index)
		{
			if (binding_present(census_right, census_left.constant_buffers[index]))
				++shared_bindings;
			else ++left_only_bindings;
		}
		for (std::size_t index{}; index < census_right.constant_buffer_count; ++index)
		{
			if (!binding_present(census_left, census_right.constant_buffers[index]))
				++right_only_bindings;
		}
		output << "  backend_gpu_census_cb_identity_only: shared=" << shared_bindings;
		output << " left_only=" << left_only_bindings;
		output << " right_only=" << right_only_bindings;
		output << " generic_content_not_sampled=yes vs_b3_content_reported_above=yes\n";
		const auto shader_stage_name = [](const std::uint8_t stage)
		{
			switch (stage)
			{
			case 0: return "vs";
			case 1: return "ps";
			case 2: return "cs";
			case 3: return "gs";
			case 4: return "hs";
			case 5: return "ds";
			default: return "unknown";
			}
		};
		std::size_t binding_differences_emitted{};
		const auto emit_binding_differences = [&](const std::size_t eye,
			const engine_stereo_gpu_census::eye_report& source,
			const engine_stereo_gpu_census::eye_report& opposite)
		{
			for (std::size_t index{}; index < source.constant_buffer_count &&
				binding_differences_emitted < 16; ++index)
			{
				const auto& value = source.constant_buffers[index];
				if (binding_present(opposite, value)) continue;
				output << "    gpu_census_cb_diff: eye=" << eye;
				output << " stage=" << shader_stage_name(value.stage);
				output << " slot=" << static_cast<std::uint32_t>(value.slot);
				output << " identity=0x" << std::hex << value.identity << std::dec;
				output << " calls=" << value.calls << '\n';
				++binding_differences_emitted;
			}
		};
		emit_binding_differences(0, census_left, census_right);
		emit_binding_differences(1, census_right, census_left);

		std::size_t resource_candidates_emitted{};
		const auto access_site_name = [](const engine_stereo_gpu_census::access_site site)
		{
			switch (site)
			{
			case engine_stereo_gpu_census::access_site::vs_srv: return "vs_srv";
			case engine_stereo_gpu_census::access_site::ps_srv: return "ps_srv";
			case engine_stereo_gpu_census::access_site::gs_srv: return "gs_srv";
			case engine_stereo_gpu_census::access_site::hs_srv: return "hs_srv";
			case engine_stereo_gpu_census::access_site::ds_srv: return "ds_srv";
			case engine_stereo_gpu_census::access_site::cs_srv: return "cs_srv";
			case engine_stereo_gpu_census::access_site::om_rtv: return "om_rtv";
			case engine_stereo_gpu_census::access_site::om_dsv: return "om_dsv";
			case engine_stereo_gpu_census::access_site::om_uav: return "om_uav";
			case engine_stereo_gpu_census::access_site::cs_uav: return "cs_uav";
			case engine_stereo_gpu_census::access_site::clear_rtv: return "clear_rtv";
			case engine_stereo_gpu_census::access_site::clear_dsv: return "clear_dsv";
			case engine_stereo_gpu_census::access_site::copy_source: return "copy_source";
			case engine_stereo_gpu_census::access_site::copy_destination:
				return "copy_destination";
			case engine_stereo_gpu_census::access_site::update_destination:
				return "update_destination";
			case engine_stereo_gpu_census::access_site::structure_count_source:
				return "structure_count_source";
			case engine_stereo_gpu_census::access_site::clear_uav: return "clear_uav";
			case engine_stereo_gpu_census::access_site::generate_mips_srv:
				return "generate_mips_srv";
			case engine_stereo_gpu_census::access_site::resolve_source:
				return "resolve_source";
			case engine_stereo_gpu_census::access_site::resolve_destination:
				return "resolve_destination";
			default: return "unknown";
			}
		};
		const auto emit_resource_access = [&](const char* const name,
			const engine_stereo_gpu_census::access_observation& access)
		{
			const auto api_name = [](const engine_stereo_gpu_census::api operation)
			{
				switch (operation)
				{
				case engine_stereo_gpu_census::api::draw_indexed: return "draw_indexed";
				case engine_stereo_gpu_census::api::draw: return "draw";
				case engine_stereo_gpu_census::api::draw_indexed_instanced:
					return "draw_indexed_instanced";
				case engine_stereo_gpu_census::api::draw_instanced: return "draw_instanced";
				case engine_stereo_gpu_census::api::draw_auto: return "draw_auto";
				case engine_stereo_gpu_census::api::draw_indexed_instanced_indirect:
					return "draw_indexed_instanced_indirect";
				case engine_stereo_gpu_census::api::draw_instanced_indirect:
					return "draw_instanced_indirect";
				case engine_stereo_gpu_census::api::dispatch: return "dispatch";
				case engine_stereo_gpu_census::api::dispatch_indirect:
					return "dispatch_indirect";
				case engine_stereo_gpu_census::api::clear_render_target:
					return "clear_render_target";
				case engine_stereo_gpu_census::api::clear_depth_stencil:
					return "clear_depth_stencil";
				case engine_stereo_gpu_census::api::copy_subresource_region:
					return "copy_subresource_region";
				case engine_stereo_gpu_census::api::copy_resource:
					return "copy_resource";
				case engine_stereo_gpu_census::api::update_subresource:
					return "update_subresource";
				case engine_stereo_gpu_census::api::copy_structure_count:
					return "copy_structure_count";
				case engine_stereo_gpu_census::api::clear_uav_uint:
					return "clear_uav_uint";
				case engine_stereo_gpu_census::api::clear_uav_float:
					return "clear_uav_float";
				case engine_stereo_gpu_census::api::generate_mips:
					return "generate_mips";
				case engine_stereo_gpu_census::api::resolve_subresource:
					return "resolve_subresource";
				default: return "unknown";
				}
			};
			output << ' ' << name << "={call=" << access.call;
			output << ",site=" << access_site_name(access.site);
			output << ",api=" << api_name(access.operation);
			output << ",slot=" << static_cast<std::uint32_t>(access.slot);
			output << ",view=0x" << std::hex << access.view_identity;
			output << ",caller=0x" << access.caller << std::dec;
			output << ",output_target=" << access.output_target_id;
			output << ",resource_target=" << access.resource_target_id;
			output << ",binding_sequence=" << access.output_binding_sequence;
			output << ",output_view=0x" << std::hex
				<< access.output_render_target_view;
			output << ",view_format=" << std::dec << access.view_format;
			output << ",view_dimension=" << access.view_dimension;
			output << ",vs/ps/cs/gs/hs/ds=0x" << std::hex
				<< access.program.vs << "/0x" << access.program.ps << "/0x"
				<< access.program.cs << "/0x" << access.program.gs << "/0x"
				<< access.program.hs << "/0x" << access.program.ds << std::dec << '}';
		};
		for (const auto& resource : gpu_census_status.resources)
		{
			if (!resource.right_first_read_before_write_candidate ||
				resource_candidates_emitted >= 16) continue;
			output << "    gpu_census_resource_candidate: identity=0x" << std::hex
				<< resource.identity;
			output << " read_mask=0x" << static_cast<std::uint32_t>(resource.read_mask);
			output << " write_mask=0x" << static_cast<std::uint32_t>(resource.write_mask)
				<< std::dec;
			const auto& descriptor = resource.descriptor;
			output << " descriptor={captured=" << (descriptor.captured ? "yes" : "no");
			output << ",valid=" << (descriptor.valid ? "yes" : "no");
			output << ",dimension=" << descriptor.dimension;
			output << ",size=" << descriptor.width << 'x' << descriptor.height;
			if (descriptor.depth) output << 'x' << descriptor.depth;
			output << ",bytes=" << descriptor.byte_width;
			output << ",mips=" << descriptor.mip_levels;
			output << ",array=" << descriptor.array_size;
			output << ",format=" << descriptor.format;
			output << ",samples=" << descriptor.sample_count << ':'
				<< descriptor.sample_quality;
			output << ",usage=" << descriptor.usage;
			output << ",bind=0x" << std::hex << descriptor.bind_flags;
			output << ",cpu=0x" << descriptor.cpu_access_flags;
			output << ",misc=0x" << descriptor.misc_flags << std::dec << '}';
			emit_resource_access("left_write", resource.first_left_write);
			emit_resource_access("right_read", resource.first_right_read);
			emit_resource_access("right_write", resource.first_right_write);
			output << " candidate_only=Clear/Copy_unobserved\n";
			if (resource.target_entry_captured)
			{
				const auto target_id = resource.target_entry_id;
				const auto& bytes = resource.target_entry_before;
				std::array<std::uint64_t, 8> qwords{};
				std::memcpy(qwords.data(), bytes.data(), bytes.size());
				std::uint16_t width{}, height{};
				std::uint32_t related{};
				std::memcpy(&width, bytes.data() + 0x30, sizeof(width));
				std::memcpy(&height, bytes.data() + 0x32, sizeof(height));
				std::memcpy(&related, bytes.data() + 0x38, sizeof(related));
				output << "    gpu_census_candidate_target_entry: target=" << target_id;
				output << " qwords=" << std::hex;
				for (std::size_t index{}; index < qwords.size(); ++index)
				{
					if (index) output << ',';
					output << "0x" << qwords[index];
				}
				output << std::dec << " size=" << width << 'x' << height;
				output << " related=" << related;
				output << " stable=" << (resource.target_entry_stable ? "yes" : "no");
				output << " rtv_match_qword=";
				bool rtv_match{};
				for (std::size_t index{}; index < qwords.size(); ++index)
				{
					if (qwords[index] != resource.first_left_write.view_identity) continue;
					if (rtv_match) output << ',';
					output << index;
					rtv_match = true;
				}
				if (!rtv_match) output << "none";
				output << " srv_match_qword=";
				bool srv_match{};
				for (std::size_t index{}; index < qwords.size(); ++index)
				{
					if (qwords[index] != resource.first_right_read.view_identity) continue;
					if (srv_match) output << ',';
					output << index;
					srv_match = true;
				}
				if (!srv_match) output << "none";
				output << '\n';
			}
			++resource_candidates_emitted;
		}
	}
}
