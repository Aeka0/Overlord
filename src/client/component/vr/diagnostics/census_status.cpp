#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "../engine_stereo_gpu_census.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr::diagnostics::detail
{
	namespace
	{
		const char* gpu_census_state_name(
			const engine_stereo_gpu_census::state value) noexcept
		{
			switch (value)
			{
			case engine_stereo_gpu_census::state::idle: return "idle";
			case engine_stereo_gpu_census::state::pair_active: return "pair_active";
			case engine_stereo_gpu_census::state::eye_active: return "eye_active";
			case engine_stereo_gpu_census::state::complete: return "complete";
			case engine_stereo_gpu_census::state::failed: return "failed";
			default: return "unknown";
			}
		}

		const char* dynamic_fx_evidence_name(
			const engine_stereo_gpu_census::dynamic_fx_evidence value) noexcept
		{
			switch (value)
			{
			case engine_stereo_gpu_census::dynamic_fx_evidence::unfinalized:
				return "unfinalized";
			case engine_stereo_gpu_census::dynamic_fx_evidence::not_observed:
				return "not_observed";
			case engine_stereo_gpu_census::dynamic_fx_evidence::invalid_shape:
				return "invalid_shape";
			case engine_stereo_gpu_census::dynamic_fx_evidence::observed:
				return "observed";
			case engine_stereo_gpu_census::dynamic_fx_evidence::truncated:
				return "truncated";
			case engine_stereo_gpu_census::dynamic_fx_evidence::incomplete:
				return "incomplete";
			default: return "unknown";
			}
		}

		const char* dynamic_fx_arena_evidence_name(
			const engine_stereo_gpu_census::dynamic_fx_arena_evidence value) noexcept
		{
			switch (value)
			{
			case engine_stereo_gpu_census::dynamic_fx_arena_evidence::unfinalized:
				return "unfinalized";
			case engine_stereo_gpu_census::dynamic_fx_arena_evidence::not_observed:
				return "not_observed";
			case engine_stereo_gpu_census::dynamic_fx_arena_evidence::complete:
				return "complete";
			case engine_stereo_gpu_census::dynamic_fx_arena_evidence::unreadable:
				return "unreadable";
			case engine_stereo_gpu_census::dynamic_fx_arena_evidence::truncated:
				return "truncated";
			default:
				return "unknown";
			}
		}

		const char* dynamic_fx_arena_phase_name(
			const engine_stereo_gpu_census::dynamic_fx_arena_phase value) noexcept
		{
			switch (value)
			{
			case engine_stereo_gpu_census::dynamic_fx_arena_phase::eye_begin:
				return "eye_begin";
			case engine_stereo_gpu_census::dynamic_fx_arena_phase::backend_view_copy:
				return "backend_view_copy";
			case engine_stereo_gpu_census::dynamic_fx_arena_phase::eye_end:
				return "eye_end";
			default:
				return "unknown";
			}
		}

		const char* dynamic_fx_arena_transition_name(
			const engine_stereo_gpu_census::dynamic_fx_arena_transition_kind value)
			noexcept
		{
			switch (value)
			{
			case engine_stereo_gpu_census::dynamic_fx_arena_transition_kind::
				output0_begin_to_view: return "output0_begin_to_view";
			case engine_stereo_gpu_census::dynamic_fx_arena_transition_kind::
				output0_view_to_end: return "output0_view_to_end";
			case engine_stereo_gpu_census::dynamic_fx_arena_transition_kind::
				output0_end_to_output1_begin:
				return "output0_end_to_output1_begin";
			case engine_stereo_gpu_census::dynamic_fx_arena_transition_kind::
				output1_begin_to_view: return "output1_begin_to_view";
			case engine_stereo_gpu_census::dynamic_fx_arena_transition_kind::
				output1_view_to_end: return "output1_view_to_end";
			case engine_stereo_gpu_census::dynamic_fx_arena_transition_kind::
				output0_view_to_output1_view:
				return "output0_view_to_output1_view";
			default: return "unknown";
			}
		}


	}

	void append_census_status(std::ostringstream& output,
		const engine_stereo_gpu_census::report& gpu_census_status)
	{
		output << "  backend_gpu_census: state="
			<< gpu_census_state_name(gpu_census_status.current_state);
		output << " pair=" << gpu_census_status.pair_id;
		output << " eyes=0x" << std::hex
			<< static_cast<std::uint32_t>(gpu_census_status.completed_eye_mask);
		output << " context=0x" << gpu_census_status.context_identity << std::dec;
		output << " owner_thread=" << gpu_census_status.owner_thread_id;
		output << " queries=" << gpu_census_status.query_calls;
		output << " query_failures=" << gpu_census_status.query_failures;
		output << " overflows=" << gpu_census_status.overflows;
		output << " overflow_domains(identity/binding/resource/depth/observation)="
			<< gpu_census_status.identity_overflows << '/'
			<< gpu_census_status.binding_overflows << '/'
			<< gpu_census_status.resource_overflows << '/'
			<< gpu_census_status.depth_overflows << '/'
			<< gpu_census_status.observation_overflows;
		output << " identity_binding_truncated="
			<< (gpu_census_status.identity_overflows != 0 ||
				gpu_census_status.binding_overflows != 0 ? "yes" : "no");
		output << " resources=" << gpu_census_status.resource_count;
		output << " api_filtered_read_before_right_write_candidates="
			<< gpu_census_status.hazard_candidates;
		output << " rejected(dispatch_graphics_srv/dispatch_om/draw_cs/null_shader/no_color_write)="
			<< gpu_census_status.rejected_dispatch_graphics_srv << '/'
			<< gpu_census_status.rejected_dispatch_om << '/'
			<< gpu_census_status.rejected_draw_cs << '/'
			<< gpu_census_status.rejected_null_shader << '/'
			<< gpu_census_status.rejected_no_color_write;
		output << " ignored_foreign_thread="
			<< gpu_census_status.foreign_thread_observations;
		output << " ignored_foreign_context="
			<< gpu_census_status.foreign_context_observations;
		output << " srv_slots=" << gpu_census_status.srv_slots_scanned;
		output << " observation_cap_per_eye="
			<< engine_stereo_gpu_census::maximum_observations_per_eye << '\n';
		const auto& dynamic_fx = gpu_census_status.dynamic_fx;
		output << "  backend_gpu_census_dynamic_fx: evidence="
			<< dynamic_fx_evidence_name(dynamic_fx.evidence);
		output << " finalized=" << (dynamic_fx.comparison_finalized ? "yes" : "no");
		output << " evidence_available="
			<< (dynamic_fx.evidence_available ? "yes" : "no");
		output << " complete=" << (dynamic_fx.comparison_complete ? "yes" : "no");
		output << " range_hits=" << dynamic_fx.range_hits;
		output << " semantic_hits=" << dynamic_fx.semantic_hits;
		output << " invalid=" << dynamic_fx.invalid_invocations;
		output << " observation_overflow=" << dynamic_fx.observation_overflows;
		output << " cb_bytes(attempted/complete/unavailable/overflow)="
			<< dynamic_fx.content_byte_capture_attempts << '/'
			<< dynamic_fx.content_byte_capture_completions << '/'
			<< dynamic_fx.content_byte_capture_unavailable << '/'
			<< dynamic_fx.content_byte_capture_overflows;
		output << " cb_reflection(attempted/complete/missing/oversized/fail/stage/"
			"buffer_overflow/variable_overflow/name_truncation)="
			<< dynamic_fx.reflection_attempts << '/'
			<< dynamic_fx.reflection_completions << '/'
			<< dynamic_fx.reflection_bytecode_missing << '/'
			<< dynamic_fx.reflection_bytecode_oversized << '/'
			<< dynamic_fx.reflection_failures << '/'
			<< dynamic_fx.reflection_stage_mismatches << '/'
			<< dynamic_fx.reflection_buffer_overflows << '/'
			<< dynamic_fx.reflection_variable_overflows << '/'
			<< dynamic_fx.reflection_name_truncations;
		output << " sample_cap_per_output_family="
			<< engine_stereo_gpu_census::maximum_dynamic_fx_observations_per_family
			<< '\n';
		const auto emit_dynamic_fx_invocation = [&](const char* const name,
			const engine_stereo_gpu_census::dynamic_fx_invocation& value)
		{
			const auto index_count = value.arguments.count >= 1 ?
				static_cast<std::uint32_t>(value.arguments.values[0]) : 0;
			const auto start_index = value.arguments.count >= 2 ?
				static_cast<std::uint32_t>(value.arguments.values[1]) : 0;
			const auto base_vertex = value.arguments.count >= 3 ?
				static_cast<std::int32_t>(value.arguments.values[2]) : 0;
			output << name << "={output_ordinal=" << value.output_ordinal;
			output << ",family_ordinal=" << value.family_ordinal;
			output << ",arena_view_copy_snapshot_sequence="
				<< value.arena_view_copy_snapshot_sequence;
			output << ",caller=0x" << std::hex << value.caller << std::dec;
			output << ",raw_arg_count=" << static_cast<std::uint32_t>(
				value.arguments.count);
			output << ",raw_args=[IndexCount=" << index_count;
			output << ",StartIndex=" << start_index;
			output << ",BaseVertex=" << base_vertex << ']';
			output << ",program(vs/ps/cs/gs/hs/ds)=0x" << std::hex
				<< value.program.vs << "/0x" << value.program.ps << "/0x"
				<< value.program.cs << "/0x" << value.program.gs << "/0x"
				<< value.program.hs << "/0x" << value.program.ds << std::dec;
			output << ",target=" << value.output_target_id;
			output << ",pipeline(layout/depth/blend/raster)=0x" << std::hex
				<< value.input_layout << "/0x" << value.depth_stencil_state
				<< "/0x" << value.blend_state << "/0x"
				<< value.rasterizer_state << std::dec;
			output << ",stencil/sample/topology=" << value.stencil_reference << '/'
				<< value.sample_mask << '/' << value.primitive_topology;
			output << ",blend_factor_bits=[" << std::hex
				<< value.blend_factor_bits[0] << ',' << value.blend_factor_bits[1]
				<< ',' << value.blend_factor_bits[2] << ','
				<< value.blend_factor_bits[3] << std::dec << ']';
			output << ",viewport(count/hash)=" << value.viewport_count << "/0x"
				<< std::hex << value.viewport_hash << std::dec;
			output << ",scissor(count/hash)=" << value.scissor_count << "/0x"
				<< std::hex << value.scissor_hash << std::dec;
			output << ",index_buffer=0x" << std::hex << value.index_buffer << std::dec;
			output << ",index_format=" << value.index_format;
			output << ",index_offset=" << value.index_offset;
			output << ",index_range(exact/hash_eligible/offset/bytes)="
				<< (value.index_range.exact ? "yes" : "no") << '/'
				<< (value.index_range.hash_eligible ? "yes" : "no") << '/'
				<< value.index_range.byte_offset << '/'
				<< value.index_range.byte_count;
			output << ",vertex_buffers=[";
			for (std::size_t slot{}; slot < value.vertex_buffers.size(); ++slot)
			{
				if (slot) output << ',';
				const auto& binding = value.vertex_buffers[slot];
				output << slot << ":0x" << std::hex << binding.buffer << std::dec
					<< '/' << binding.stride << '/' << binding.offset;
			}
			output << ']';
			output << ",binding_hash(cb/srv)=0x" << std::hex
				<< value.bindings.constant_buffers << "/0x"
				<< value.bindings.shader_resources << std::dec;
			output << ",binding_count(cb/srv)="
				<< value.bindings.constant_buffer_count << '/'
				<< value.bindings.shader_resource_count << '}';
		};
		const auto dynamic_fx_stage_name = [](const std::uint8_t stage)
		{
			switch (stage)
			{
			case 0: return "vs";
			case 1: return "ps";
			default: return "unexpected";
			}
		};
		const auto& dynamic_fx_stream = dynamic_fx.stream;
		output << "    gpu_census_dynamic_fx_stream: finalized="
			<< (dynamic_fx_stream.finalized ? "yes" : "no");
		output << " complete=" << (dynamic_fx_stream.complete ? "yes" : "no");
		output << " observations(output0/output1)="
			<< dynamic_fx_stream.output0_observations << '/'
			<< dynamic_fx_stream.output1_observations;
		output << " compared/dynamic/matched="
			<< dynamic_fx_stream.compared_ordinals << '/'
			<< dynamic_fx_stream.dynamic_ordinals << '/'
			<< dynamic_fx_stream.matched_family_invocations;
		output << " divergence(family/semantic/missing0/missing1)="
			<< dynamic_fx_stream.family_transitions << '/'
			<< dynamic_fx_stream.semantic_mismatches << '/'
			<< dynamic_fx_stream.missing_output0 << '/'
			<< dynamic_fx_stream.missing_output1;
		output << " binding_comparisons="
			<< dynamic_fx_stream.binding_comparisons << '\n';
		const auto emit_dynamic_fx_stream_window = [&](const char* const kind,
			const engine_stereo_gpu_census::dynamic_fx_stream_window& window)
		{
			if (!window.captured) return;
			output << "      gpu_census_dynamic_fx_stream_window: kind=" << kind;
			output << " event_output_ordinal=" << window.event_output_ordinal << '\n';
			constexpr std::array<const char*,
				engine_stereo_gpu_census::dynamic_fx_stream_window_size>
				positions{"previous", "event", "next"};
			for (std::size_t position{}; position < window.ordinals.size(); ++position)
			{
				const auto& ordinal = window.ordinals[position];
				output << "        gpu_census_dynamic_fx_stream_ordinal: position="
					<< positions[position] << " output_ordinal="
					<< ordinal.output_ordinal;
				for (std::size_t eye{}; eye < ordinal.outputs.size(); ++eye)
				{
					const auto& entry = ordinal.outputs[eye];
					output << " output" << eye << "(present/semantic/family)="
						<< (entry.present ? "yes" : "no") << '/'
						<< (entry.semantic ? "yes" : "no") << '/'
						<< dynamic_fx_family_name(entry.family);
					if (entry.present)
					{
						output << ' ';
						emit_dynamic_fx_invocation(eye == 0 ? "output0" : "output1",
							entry.invocation);
					}
				}
				output << '\n';
			}
		};
		emit_dynamic_fx_stream_window("family_transition",
			dynamic_fx_stream.first_family_transition);
		emit_dynamic_fx_stream_window("semantic_mismatch",
			dynamic_fx_stream.first_semantic_mismatch);
		emit_dynamic_fx_stream_window("missing_output0",
			dynamic_fx_stream.first_missing_output0);
		emit_dynamic_fx_stream_window("missing_output1",
			dynamic_fx_stream.first_missing_output1);
		constexpr std::size_t dynamic_fx_raw_emit_cap = 8;
		for (std::size_t family_index{}; family_index < dynamic_fx.families.size();
			++family_index)
		{
			const auto family = static_cast<engine_stereo_gpu_census::dynamic_fx_family>(
				family_index);
			const auto& comparison = dynamic_fx.families[family_index];
			const auto& output0 = dynamic_fx.eyes[0].families[family_index];
			const auto& output1 = dynamic_fx.eyes[1].families[family_index];
			const auto truncated = output0.observation_overflows != 0 ||
				output1.observation_overflows != 0;
			output << "    gpu_census_dynamic_fx_family: family="
				<< dynamic_fx_family_name(family);
			output << " finalized="
				<< (comparison.comparison_finalized ? "yes" : "no");
			output << " evidence_available="
				<< (comparison.evidence_available ? "yes" : "no");
			output << " complete=" << (comparison.comparison_complete ? "yes" : "no");
			output << " truncated=" << (truncated ? "yes" : "no");
			output << " range(output0/output1)=" << output0.range_hits << '/'
				<< output1.range_hits;
			output << " invalid(output0/output1)=" << output0.invalid_invocations << '/'
				<< output1.invalid_invocations;
			output << " calls(output0/output1)=" << comparison.output0_calls << '/'
				<< comparison.output1_calls;
			output << " stored(output0/output1)=" << output0.observation_count << '/'
				<< output1.observation_count;
			output << " dropped(output0/output1)=" << output0.observation_overflows << '/'
				<< output1.observation_overflows;
			output << " zero_IndexCount(output0/output1)="
				<< comparison.output0_zero_index_counts << '/'
				<< comparison.output1_zero_index_counts;
			output << " missing(output0/output1)=" << comparison.missing_output0 << '/'
				<< comparison.missing_output1;
			output << " paired/compared/global_ordinal_matches="
				<< comparison.paired_calls << '/' << comparison.compared_calls << '/'
				<< comparison.global_ordinal_matches << '\n';
			output << "      gpu_census_dynamic_fx_compare: family="
				<< dynamic_fx_family_name(family);
			output << " mismatch(index_count/base_vertex/global_ordinal/program/caller/target/index_buffer/vertex_buffer/vertex_stride/vertex_offset/arg_shape)="
				<< comparison.index_count_mismatches << '/'
				<< comparison.base_vertex_mismatches << '/'
				<< comparison.ordinal_mismatches << '/'
				<< comparison.program_mismatches << '/'
				<< comparison.caller_mismatches << '/'
				<< comparison.output_target_mismatches << '/'
				<< comparison.index_buffer_mismatches << '/'
				<< comparison.vertex_buffer_mismatches << '/'
				<< comparison.vertex_stride_mismatches << '/'
				<< comparison.vertex_offset_mismatches << '/'
				<< comparison.argument_shape_mismatches;
			output << " vertex_slots(buffer/stride/offset)=[";
			for (std::size_t slot{};
				slot < engine_stereo_gpu_census::dynamic_fx_vertex_binding_count; ++slot)
			{
				if (slot) output << ',';
				output << slot << ':'
					<< comparison.vertex_buffer_mismatches_by_slot[slot] << '/'
					<< comparison.vertex_stride_mismatches_by_slot[slot] << '/'
					<< comparison.vertex_offset_mismatches_by_slot[slot];
			}
			output << ']';
			output << " delta(eligible/ineligible/nonzero)="
				<< comparison.start_index_delta_eligible << '/'
				<< comparison.start_index_delta_ineligible << '/'
				<< comparison.nonzero_start_index_deltas;
			output << " delta_observed="
				<< (comparison.start_index_delta_observed ? "yes" : "no");
			output << " delta_first/min/max=" << comparison.start_index_delta_first << '/'
				<< comparison.start_index_delta_min << '/'
				<< comparison.start_index_delta_max;
			output << " delta_constant="
				<< (comparison.start_index_delta_constant ? "yes" : "no");
			output << " mismatch_samples(stored/dropped)="
				<< comparison.mismatch_sample_count << '/'
				<< comparison.mismatch_sample_overflows << '\n';
			output << "      gpu_census_dynamic_fx_pipeline: family="
				<< dynamic_fx_family_name(family);
			output << " mismatch(layout/depth/blend_state/blend_factor/raster/topology/viewport/scissor)="
				<< comparison.input_layout_mismatches << '/'
				<< comparison.depth_state_mismatches << '/'
				<< comparison.blend_state_mismatches << '/'
				<< comparison.blend_factor_mismatches << '/'
				<< comparison.rasterizer_state_mismatches << '/'
				<< comparison.topology_mismatches << '/'
				<< comparison.viewport_mismatches << '/'
				<< comparison.scissor_mismatches << '\n';
			output << "      gpu_census_dynamic_fx_bindings: family="
				<< dynamic_fx_family_name(family);
			output << " signature_mismatch(cb/srv/sampler)="
				<< comparison.constant_buffer_signature_mismatches << '/'
				<< comparison.shader_resource_signature_mismatches << '/'
				<< comparison.sampler_signature_mismatches;
			output << " cb(slot/identity/content_compared/content_mismatch/unknown)="
				<< comparison.constant_buffer_slot_comparisons << '/'
				<< comparison.constant_buffer_identity_mismatches << '/'
				<< comparison.constant_buffer_content_comparisons << '/'
				<< comparison.constant_buffer_content_mismatches << '/'
				<< comparison.constant_buffer_content_unknown;
			output << " ps_srv(compared/mismatch)="
				<< comparison.ps_shader_resource_comparisons << '/'
				<< comparison.ps_shader_resource_mismatches;
			output << " ps_sampler(compared/identity_mismatch/descriptor_mismatch)="
				<< comparison.ps_sampler_comparisons << '/'
				<< comparison.ps_sampler_identity_mismatches << '/'
				<< comparison.ps_sampler_descriptor_mismatches;
			output << " detail_drops=" << comparison.binding_detail_drops;
			output << " samples(cb_stored/dropped,ps_srv_stored/dropped,"
				"ps_sampler_stored/dropped)="
				<< comparison.constant_buffer_sample_count << '/'
				<< comparison.constant_buffer_sample_overflows << ','
				<< comparison.ps_shader_resource_sample_count << '/'
				<< comparison.ps_shader_resource_sample_overflows << ','
				<< comparison.ps_sampler_sample_count << '/'
				<< comparison.ps_sampler_sample_overflows << '\n';

			for (std::size_t output_index{}; output_index < 2; ++output_index)
			{
				const auto& output_family = dynamic_fx.eyes[output_index].families[
					family_index];
				const auto emit_count = (std::min)(dynamic_fx_raw_emit_cap,
					output_family.observation_count);
				for (std::size_t sample_index{}; sample_index < emit_count;
					++sample_index)
				{
					output << "      gpu_census_dynamic_fx_raw: family="
						<< dynamic_fx_family_name(family);
					output << " output=output" << output_index;
					output << " sample=" << sample_index << ' ';
					emit_dynamic_fx_invocation("invocation",
						output_family.observations[sample_index]);
					output << '\n';
				}
			}
			for (std::size_t sample_index{};
				sample_index < comparison.mismatch_sample_count; ++sample_index)
			{
				const auto& sample = comparison.mismatch_samples[sample_index];
				output << "      gpu_census_dynamic_fx_mismatch_sample: family="
					<< dynamic_fx_family_name(family);
				output << " family_ordinal=" << sample.family_ordinal;
				output << " output0_present="
					<< (sample.output0_present ? "yes" : "no");
				output << " output1_present="
					<< (sample.output1_present ? "yes" : "no");
				if (sample.output0_present)
				{
					output << ' ';
					emit_dynamic_fx_invocation("output0", sample.output0);
				}
				if (sample.output1_present)
				{
					output << ' ';
					emit_dynamic_fx_invocation("output1", sample.output1);
				}
				output << '\n';
			}
			for (std::size_t sample_index{};
				sample_index < comparison.constant_buffer_sample_count; ++sample_index)
			{
				const auto& sample = comparison.constant_buffer_samples[sample_index];
				output << "      gpu_census_dynamic_fx_cb_sample: family="
					<< dynamic_fx_family_name(family);
				output << " output_ordinal=" << sample.output_ordinal;
				output << " family_ordinal=" << sample.family_ordinal;
				output << " output_family_ordinals="
					<< sample.output0_family_ordinal << '/'
					<< sample.output1_family_ordinal;
				output << " output_ordinals=" << sample.output0_output_ordinal << '/'
					<< sample.output1_output_ordinal;
				output << " occurrences=" << sample.occurrences;
				output << " output_ordinal_range=" << sample.output_ordinal << '/'
					<< sample.last_output_ordinal;
				output << " program(output0_vs/ps,output1_vs/ps)=0x" << std::hex
					<< sample.output0_program.vs << "/0x" << sample.output0_program.ps
					<< ",0x" << sample.output1_program.vs << "/0x"
					<< sample.output1_program.ps << std::dec;
				output << " stage=" << dynamic_fx_stage_name(sample.stage);
				output << " slot=" << static_cast<std::uint32_t>(sample.slot);
				output << " present(output0/output1)="
					<< (sample.output0_present ? "yes" : "no") << '/'
					<< (sample.output1_present ? "yes" : "no");
				output << " reason(identity/content_unknown/content_mismatch)="
					<< (sample.identity_mismatch ? "yes" : "no") << '/'
					<< (sample.content_unknown ? "yes" : "no") << '/'
					<< (sample.content_mismatch ? "yes" : "no");
				const auto emit_content = [&](const char* const name,
					const engine_stereo_gpu_census::dynamic_fx_constant_buffer_binding&
						binding)
				{
					output << ' ' << name << "={identity=0x" << std::hex
						<< binding.identity << std::dec;
					output << ",known=" << (binding.content.known ? "yes" : "no");
					output << ",creation=" << binding.content.creation_serial;
					output << ",upload=" << binding.content.upload_generation;
					output << ",hash=0x" << std::hex << binding.content.hash_low
						<< ":0x" << binding.content.hash_high << std::dec;
					output << ",bytes=" << binding.content.byte_width;
					output << ",source=" << static_cast<std::uint32_t>(
						binding.content.source);
					output << ",caller=0x" << std::hex
						<< binding.content.upload_caller << std::dec;
					output << ",thread=" << binding.content.upload_thread << '}';
				};
				if (sample.output0_present) emit_content("output0", sample.output0);
				if (sample.output1_present) emit_content("output1", sample.output1);
				output << " byte_diff(available/complete/compared/different/first/last)="
					<< (sample.byte_comparison_available ? "yes" : "no") << '/'
					<< (sample.byte_comparison_complete ? "yes" : "no") << '/'
					<< sample.byte_compared << '/' << sample.byte_difference_count << '/'
					<< sample.first_byte_difference << '/'
					<< sample.last_byte_difference;
				output << " byte_samples=[";
				for (std::size_t difference{};
					difference < sample.byte_difference_sample_count; ++difference)
				{
					if (difference) output << ',';
					output << sample.byte_difference_offsets[difference] << ":0x"
						<< std::hex << static_cast<std::uint32_t>(
							sample.output0_difference_values[difference]) << "->0x"
						<< static_cast<std::uint32_t>(
							sample.output1_difference_values[difference]) << std::dec;
				}
				output << ']';
				output << " word_diff(compared/different/first/last)="
					<< sample.word_compared << '/' << sample.word_difference_count << '/'
					<< sample.first_word_difference << '/'
					<< sample.last_word_difference;
				output << " word_samples=[";
				for (std::size_t difference{};
					difference < sample.word_difference_sample_count; ++difference)
				{
					if (difference) output << ',';
					const auto left_bits = sample.output0_word_values[difference];
					const auto right_bits = sample.output1_word_values[difference];
					output << sample.word_difference_offsets[difference] << ":0x"
						<< std::hex << left_bits << "->0x" << right_bits << std::dec
						<< '(' << float_from_bits(left_bits) << "->"
						<< float_from_bits(right_bits) << ')';
				}
				output << ']';
				output << '\n';
				const auto emit_reflection = [&](const char* const eye,
					const engine_stereo_gpu_census::
						dynamic_fx_constant_buffer_reflection& reflection)
				{
					output << "        gpu_census_dynamic_fx_cb_reflection: family="
						<< dynamic_fx_family_name(family);
					output << " output_ordinal=" << sample.output_ordinal;
					output << " output=" << eye;
					output << " shader=0x" << std::hex << reflection.shader << std::dec;
					output << " attempted/available/declared/complete="
						<< yes_no(reflection.attempted) << '/'
						<< yes_no(reflection.available) << '/'
						<< yes_no(reflection.binding_declared) << '/'
						<< yes_no(reflection.complete);
					output << " shader_name=\""
						<< reflection.shader_debug_name.data() << "\"";
					output << " shader_name_hash=0x" << std::hex
						<< reflection.shader_debug_name_hash << std::dec;
					output << " buffer=\"" << reflection.buffer_name.data() << "\"";
					output << " buffer_hash=0x" << std::hex
						<< reflection.buffer_name_hash << std::dec;
					output << " buffer_size=" << reflection.buffer_size;
					output << " mapped/unmapped="
						<< reflection.mapped_differing_bytes << '/'
						<< reflection.unmapped_differing_bytes;
					output << " variables(stored/dropped)="
						<< reflection.variable_count << '/'
						<< reflection.variable_overflows << '\n';
					for (std::size_t variable_index{};
						variable_index < reflection.variable_count; ++variable_index)
					{
						const auto& variable = reflection.variables[variable_index];
						output << "          gpu_census_dynamic_fx_cb_variable: family="
							<< dynamic_fx_family_name(family);
						output << " output_ordinal=" << sample.output_ordinal;
						output << " output=" << eye;
						output << " name=\"" << variable.name.data() << "\"";
						output << " name_hash=0x" << std::hex << variable.name_hash
							<< std::dec;
						output << " range=" << variable.start_offset << '+'
							<< variable.size;
						output << " differences(count/first/last)="
							<< variable.differing_bytes << '/'
							<< variable.first_difference << '/'
							<< variable.last_difference << '\n';
					}
				};
				emit_reflection("output0", sample.output0_reflection);
				emit_reflection("output1", sample.output1_reflection);
			}
			for (std::size_t sample_index{};
				sample_index < comparison.ps_shader_resource_sample_count;
				++sample_index)
			{
				const auto& sample = comparison.ps_shader_resource_samples[sample_index];
				output << "      gpu_census_dynamic_fx_ps_srv_sample: family="
					<< dynamic_fx_family_name(family);
				output << " output_ordinal=" << sample.output_ordinal;
				output << " family_ordinal=" << sample.family_ordinal;
				output << " output_family_ordinals="
					<< sample.output0_family_ordinal << '/'
					<< sample.output1_family_ordinal;
				output << " slot=" << static_cast<std::uint32_t>(sample.slot);
				output << " present(output0/output1)="
					<< (sample.output0_present ? "yes" : "no") << '/'
					<< (sample.output1_present ? "yes" : "no");
				const auto emit_binding = [&](const char* const name,
					const engine_stereo_gpu_census::dynamic_fx_shader_resource_binding&
						value)
				{
					const auto& binding = value.binding;
					output << ' ' << name << "={view/resource=0x" << std::hex
						<< binding.view << "/0x" << binding.resource << std::dec;
					output << ",format/dimension=" << binding.range.format << '/'
						<< binding.range.dimension;
					output << ",mip=" << binding.range.most_detailed_mip << '+'
						<< binding.range.mip_levels;
					output << ",array=" << binding.range.first_array_slice << '+'
						<< binding.range.array_size;
					output << ",elements=" << binding.range.first_element << '+'
						<< binding.range.element_count << '}';
				};
				if (sample.output0_present) emit_binding("output0", sample.output0);
				if (sample.output1_present) emit_binding("output1", sample.output1);
				output << '\n';
			}
			for (std::size_t sample_index{};
				sample_index < comparison.ps_sampler_sample_count; ++sample_index)
			{
				const auto& sample = comparison.ps_sampler_samples[sample_index];
				output << "      gpu_census_dynamic_fx_ps_sampler_sample: family="
					<< dynamic_fx_family_name(family);
				output << " output_ordinal=" << sample.output_ordinal;
				output << " family_ordinal=" << sample.family_ordinal;
				output << " output_family_ordinals="
					<< sample.output0_family_ordinal << '/'
					<< sample.output1_family_ordinal;
				output << " slot=" << static_cast<std::uint32_t>(sample.slot);
				output << " present(output0/output1)="
					<< yes_no(sample.output0_present) << '/'
					<< yes_no(sample.output1_present);
				output << " mismatch(identity/descriptor)="
					<< yes_no(sample.identity_mismatch) << '/'
					<< yes_no(sample.descriptor_mismatch);
				const auto emit_sampler = [&](const char* const name,
					const engine_stereo_gpu_census::dynamic_fx_sampler_binding& value)
				{
					const auto& descriptor = value.descriptor;
					output << ' ' << name << "={identity=0x" << std::hex
						<< value.identity << std::dec;
					output << ",captured=" << yes_no(descriptor.captured);
					output << ",filter=" << descriptor.filter;
					output << ",address=" << descriptor.address_u << '/'
						<< descriptor.address_v << '/' << descriptor.address_w;
					output << ",lod_bits=0x" << std::hex
						<< descriptor.mip_lod_bias_bits << "/0x"
						<< descriptor.minimum_lod_bits << "/0x"
						<< descriptor.maximum_lod_bits << std::dec;
					output << ",anisotropy=" << descriptor.maximum_anisotropy;
					output << ",comparison=" << descriptor.comparison_function << '}';
				};
				if (sample.output0_present) emit_sampler("output0", sample.output0);
				if (sample.output1_present) emit_sampler("output1", sample.output1);
				output << '\n';
			}
		}
		const auto& arena = dynamic_fx.arena;
		output << "  backend_gpu_census_dynamic_fx_arena: evidence="
			<< dynamic_fx_arena_evidence_name(arena.evidence);
		output << " finalized=" << (arena.finalized ? "yes" : "no");
		output << " snapshots(attempted/completed/unreadable/overflow)="
			<< arena.snapshot_attempts << '/' << arena.snapshot_completions << '/'
			<< arena.unreadable << '/' << arena.overflows;
		output << " order=" << arena.order_mismatches;
		output << " foreign(pair/output/thread)=" << arena.foreign_pair << '/'
			<< arena.foreign_output << '/' << arena.foreign_thread;
		output << " boundary_global_backend_identity(compared/mismatch)="
			<< arena.data_identity_comparisons << '/'
			<< arena.data_identity_mismatches;
		output << " cross_output_identity(global_compared/mismatch/backend_compared/mismatch)="
			<< arena.cross_output_global_identity_comparisons << '/'
			<< arena.cross_output_global_identity_mismatches << '/'
			<< arena.cross_output_backend_identity_comparisons << '/'
			<< arena.cross_output_backend_identity_mismatches;
		output << " invocations(linked/unlinked)=" << arena.linked_invocations << '/'
			<< arena.unlinked_invocations;
		output << " next_sequence=" << arena.next_sequence << '\n';
		for (std::size_t output_index{}; output_index < arena.outputs.size();
			++output_index)
		{
			const auto& arena_output = arena.outputs[output_index];
			output << "    gpu_census_dynamic_fx_arena_output: output=output"
				<< output_index;
			output << " snapshots(attempted/stored/completed/unreadable/overflow)="
				<< arena_output.snapshot_attempts << '/'
				<< arena_output.snapshot_count << '/'
				<< arena_output.snapshot_completions << '/'
				<< arena_output.unreadable << '/' << arena_output.overflows;
			output << " order=" << arena_output.order_mismatches;
			output << " view_copy(attempted/stored)="
				<< arena_output.view_copy_attempts << '/'
				<< arena_output.view_copy_stored;
			output << " invocations(linked/unlinked)="
				<< arena_output.linked_invocations << '/'
				<< arena_output.unlinked_invocations << '\n';
			for (std::size_t snapshot_index{};
				snapshot_index < arena_output.snapshot_count; ++snapshot_index)
			{
				const auto& snapshot = arena_output.snapshots[snapshot_index];
				output << "      gpu_census_dynamic_fx_arena_snapshot: sequence="
					<< snapshot.sequence << " output=output" << snapshot.output;
				output << " phase=" << dynamic_fx_arena_phase_name(snapshot.phase);
				output << " view_copy_ordinal=" << snapshot.view_copy_ordinal;
				output << " owner_record=0x" << std::hex << snapshot.owner_record;
				output << " backend_state=0x" << snapshot.backend_state << std::dec;
				output << " data_identity(comparable/equal)="
					<< (snapshot.data_identity_comparable ? "yes" : "no") << '/'
					<< (snapshot.data_identity_equal ? "yes" : "no") << '\n';
				const auto emit_candidate = [&](const char* const role,
					const engine_stereo_gpu_census::
						dynamic_fx_arena_candidate_snapshot& candidate)
				{
					output << "        gpu_census_dynamic_fx_arena_candidate: sequence="
						<< snapshot.sequence << " output=output" << snapshot.output;
					output << " role=" << role;
					output << " applicable=" << (candidate.applicable ? "yes" : "no");
					output << " pointer_slot=0x" << std::hex
						<< candidate.pointer_slot << std::dec;
					output << " pointer_slot_readable="
						<< (candidate.pointer_slot_readable ? "yes" : "no");
					output << " data=0x" << std::hex << candidate.data_identity
						<< std::dec;
					output << " complete=" << (candidate.complete ? "yes" : "no");
					for (std::size_t mesh_index{};
						mesh_index < candidate.meshes.size(); ++mesh_index)
					{
						const auto& mesh = candidate.meshes[mesh_index];
						output << " mesh" << mesh_index << "={address=0x" << std::hex
							<< mesh.address << std::dec;
						output << ",readable=" << (mesh.readable ? "yes" : "no");
						output << ",raw_qwords=[";
						for (std::size_t qword{}; qword < mesh.raw_qwords.size();
							++qword)
						{
							if (qword) output << ',';
							output << "0x" << std::hex << mesh.raw_qwords[qword]
								<< std::dec;
						}
						output << "]}";
					}
					output << '\n';
				};
				emit_candidate("global", snapshot.global);
				emit_candidate("backend", snapshot.backend);
			}
		}
		for (const auto& transition : arena.transitions)
		{
			output << "    gpu_census_dynamic_fx_arena_transition: kind="
				<< dynamic_fx_arena_transition_name(transition.kind);
			output << " observed=" << (transition.observed ? "yes" : "no");
			output << " sequence(from/to)=" << transition.from_sequence << '/'
				<< transition.to_sequence;
			output << " identity(global_comparable/equal/backend_comparable/equal)="
				<< (transition.global_identity_comparable ? "yes" : "no") << '/'
				<< (transition.global_identity_equal ? "yes" : "no") << '/'
				<< (transition.backend_identity_comparable ? "yes" : "no") << '/'
				<< (transition.backend_identity_equal ? "yes" : "no");
			output << " raw_comparable(global/backend)="
				<< (transition.global_raw_comparable ? "yes" : "no") << '/'
				<< (transition.backend_raw_comparable ? "yes" : "no");
			output << " changed_qword_masks(global/backend)=";
			for (std::size_t mesh{};
				mesh < transition.global_changed_qword_masks.size(); ++mesh)
			{
				if (mesh) output << ',';
				output << "0x" << std::hex
					<< static_cast<std::uint32_t>(
						transition.global_changed_qword_masks[mesh])
					<< std::dec;
			}
			output << '/';
			for (std::size_t mesh{};
				mesh < transition.backend_changed_qword_masks.size(); ++mesh)
			{
				if (mesh) output << ',';
				output << "0x" << std::hex
					<< static_cast<std::uint32_t>(
						transition.backend_changed_qword_masks[mesh])
					<< std::dec;
			}
			output << '\n';
		}
	}
}
