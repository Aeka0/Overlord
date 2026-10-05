#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "../engine_stereo_effect_timeline.hpp"
#include "../engine_stereo_execution.hpp"
#include "../engine_stereo_owner_pass.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr::diagnostics::detail
{
	void append_effect_status(std::ostringstream& output,
		const engine_stereo_owner_pass::report& owner_pass_status,
		const engine_stereo_effect_timeline::report& effect_timeline_status)
	{
		output << "  backend_gpu_census_control: request="
			<< owner_pass_status.gpu_census_request_sequence;
		output << " applied=" << owner_pass_status.gpu_census_applied_sequence;
		output << " pending=" << (owner_pass_status.gpu_census_request_sequence !=
			owner_pass_status.gpu_census_applied_sequence ? "yes" : "no");
		output << " applying=" << (owner_pass_status.gpu_census_rearm_applying ?
			"yes" : "no");
		output << " delay=" << owner_pass_status.gpu_census_requested_delay;
		output << " remaining=" << owner_pass_status.gpu_census_delay_remaining;
		output << " reset=" << owner_pass_status.gpu_census_reset_attempts << '/'
			<< owner_pass_status.gpu_census_reset_failures;
		output << " launch=" << owner_pass_status.gpu_census_launch_attempts << '/'
			<< owner_pass_status.gpu_census_launch_failures;
		output << " launch_attempted=" <<
			(owner_pass_status.gpu_census_launch_attempted ? "yes" : "no");
		output << " capture_pair=" << owner_pass_status.gpu_census_last_capture_pair
			<< '\n';
		output << "  backend_effect_timeline: state="
			<< engine_stereo_effect_timeline::to_string(effect_timeline_status.current);
		output << " installed/observers/history="
			<< yes_no(effect_timeline_status.installed) << '/'
			<< yes_no(effect_timeline_status.observers_attached) << '/'
			<< yes_no(effect_timeline_status.history_tracking_active);
		output << " context=0x" << std::hex << effect_timeline_status.expected_context
			<< std::dec << " generation=" << effect_timeline_status.device_generation;
		output << " arm=" << effect_timeline_status.arm_requests << '/'
			<< effect_timeline_status.arm_applications;
		output << " mark=" << effect_timeline_status.mark_requests << '/'
			<< effect_timeline_status.mark_applications;
		output << " pairs(start/complete/incomplete/overwritten)="
			<< effect_timeline_status.pairs_started << '/'
			<< effect_timeline_status.pairs_completed << '/'
			<< effect_timeline_status.pairs_incomplete << '/'
			<< effect_timeline_status.pairs_overwritten;
		output << " range=" << effect_timeline_status.capture_first_pair << "->"
			<< effect_timeline_status.capture_last_pair;
		output << " frozen=" << effect_timeline_status.frozen_pair;
		output << " samples=" << effect_timeline_status.sample_count << '\n';
		output << "  backend_effect_timeline_callbacks: invocation/resource/clear="
			<< effect_timeline_status.invocation_callbacks << '/'
			<< effect_timeline_status.resource_callbacks << '/'
			<< effect_timeline_status.clear_callbacks;
		output << " foreign(thread/context)="
			<< effect_timeline_status.foreign_thread_callbacks << '/'
			<< effect_timeline_status.foreign_context_callbacks;
		output << " watched(count/overflow/writes)="
			<< effect_timeline_status.watched_resource_count << '/'
			<< effect_timeline_status.watched_resource_overflows << '/'
			<< effect_timeline_status.watched_resource_writes;
		output << " stream_overflow="
			<< effect_timeline_status.compared_stream_overflows << '\n';
		if (effect_timeline_status.current ==
			engine_stereo_effect_timeline::state::frozen)
		{
			static constexpr std::array<const char*, 4> family_names{
				"code_trans", "glass", "mark", "spark"};
			static constexpr std::array<const char*, 5> stream_names{
				"ssr", "code_trans", "glass", "mark", "spark"};
			const auto alignment_name = [](const
				engine_stereo_effect_timeline::stream_alignment value) noexcept
			{
				switch (value)
				{
				case engine_stereo_effect_timeline::stream_alignment::extra_output0:
					return "extra_output0";
				case engine_stereo_effect_timeline::stream_alignment::extra_output1:
					return "extra_output1";
				case engine_stereo_effect_timeline::stream_alignment::replacement:
					return "replacement";
				case engine_stereo_effect_timeline::stream_alignment::tail_count:
					return "tail_count";
				default: return "unclassified";
				}
			};
			const auto print_words = [&output](const auto& words)
			{
				for (const auto word : words) output << '/' << std::hex << word;
				output << std::dec;
			};
			for (std::size_t sample_index{};
				sample_index < effect_timeline_status.sample_count; ++sample_index)
			{
				const auto& sample = effect_timeline_status.samples[sample_index];
				output << "    effect_timeline_pair: capture=" << sample.capture_sequence;
				output << " pair=" << sample.pair_id;
				output << " tick=" << sample.started_tick << "->" << sample.completed_tick;
				output << " complete/mask=" << yes_no(sample.completed) << "/0x"
					<< std::hex << static_cast<std::uint32_t>(sample.completed_eye_mask)
					<< std::dec;
				for (std::size_t eye{}; eye < sample.eyes.size(); ++eye)
				{
					const auto& value = sample.eyes[eye];
					const auto& record = value.end_record.valid ? value.end_record :
						value.begin_record;
					output << " eye[" << eye << "](inv/ssr)=" << value.invocations
						<< '/' << value.ssr.calls;
					output << " record(view/history/ssr)=0x" << std::hex
						<< record.current_view_hash << '/' << record.history_view_hash
						<< '/' << record.ssr_source_hash << std::dec;
				}
				output << '\n';

				for (std::size_t eye{}; eye < sample.eyes.size(); ++eye)
				{
					const auto& value = sample.eyes[eye];
					const auto& record = value.end_record.valid ? value.end_record :
						value.begin_record;
					output << "      effect_timeline_eye: eye=" << eye;
					output << " eye_offset_bits=";
					print_words(record.eye_offset_bits);
					output << " ssr_previous_eye_bits=";
					print_words(record.ssr_previous_eye_bits);
					output << " ssr_parameter_bits=";
					print_words(record.ssr_parameter_bits);
					output << " ssr_clip_lookup_bits=";
					print_words(record.ssr_clip_lookup_bits);
					output << " ssr_clip_to_fade_bits=";
					print_words(record.ssr_clip_to_fade_bits);
					output << '\n';
					const auto& ssr = value.ssr;
					output << "      effect_timeline_ssr: eye=" << eye;
					output << " calls=" << ssr.calls;
					output << " ps/blend=0x" << std::hex << ssr.pixel_shader << '/'
						<< ssr.blend_state;
					output << " srv/cb=0x" << ssr.shader_resource_hash << '/'
						<< ssr.constant_buffer_hash << std::dec;
					output << " writers=" << ssr.writer_known << '/'
						<< ssr.writer_unknown;
					for (const auto slot : {std::size_t{10}, std::size_t{13}})
					{
						const auto& resource = ssr.shader_resources[slot];
						output << " t" << slot << "(res/wseq/wpair/weye/caller/op)=0x"
							<< std::hex << resource.resource << std::dec << '/'
							<< resource.last_writer.sequence << '/'
							<< resource.last_writer.pair_id << '/'
							<< resource.last_writer.eye << "/0x" << std::hex
							<< resource.last_writer.caller << std::dec << '/'
							<< static_cast<std::uint32_t>(
								resource.last_writer.operation);
					}
					output << '\n';
				}

				if (sample.pair_id == effect_timeline_status.frozen_pair)
				{
					const auto resource_overlap = [](const auto& left,
						const auto& right) noexcept
					{
						for (std::size_t left_index{};
							left_index < left.output_view_count; ++left_index)
						{
							const auto resource = left.output_views[left_index].resource;
							if (resource == 0) continue;
							for (std::size_t right_index{};
								right_index < right.output_view_count; ++right_index)
							{
								if (right.output_views[right_index].resource == resource)
									return true;
							}
						}
						return false;
					};
					for (std::size_t target_index{};
						target_index < engine_stereo_effect_timeline::tracked_target_count;
						++target_index)
					{
						const auto& left = sample.eyes[0].targets[target_index];
						const auto& right = sample.eyes[1].targets[target_index];
						const auto resources_observed = left.output_view_count != 0 &&
							right.output_view_count != 0;
						output << "      effect_timeline_target_pair: target="
							<< engine_stereo_effect_timeline::tracked_target_ids[target_index];
						output << " resource_shared=" << (resources_observed ?
							(resource_overlap(left, right) ? "yes" : "no") : "unknown");
						output << " clear_overflow="
							<< sample.eyes[0].clear_event_overflow << '/'
							<< sample.eyes[1].clear_event_overflow << '\n';
						for (std::size_t eye{}; eye < sample.eyes.size(); ++eye)
						{
							const auto& value = sample.eyes[eye].targets[target_index];
							output << "        effect_timeline_target_eye: eye=" << eye;
							output << " inv=" << value.invocation_calls;
							output << " api(draw_indexed/draw/dispatch)="
								<< value.per_api[static_cast<std::size_t>(
									engine_stereo_execution::api::draw_indexed)] << '/'
								<< value.per_api[static_cast<std::size_t>(
									engine_stereo_execution::api::draw)] << '/'
								<< value.per_api[static_cast<std::size_t>(
									engine_stereo_execution::api::dispatch)];
							output << " events(inv/clear-first/clear-last)="
								<< value.first_invocation_event << '/'
								<< value.first_clear_event << '/'
								<< value.last_clear_event;
							output << " clear(binding/resource/exact/mismatch)="
								<< value.binding_clear_calls << '/'
								<< value.resource_clear_calls << '/'
								<< value.exact_clear_calls << '/'
								<< value.mismatched_clear_calls;
							output << " clear_before_first_inv=" << yes_no(
								value.first_clear_event != 0 &&
								value.first_clear_event < value.first_invocation_event);
							output << " views=" << value.output_view_count;
							output << " view_overflow/query_failures="
								<< value.output_view_overflow << '/'
								<< value.output_resource_query_failures;
							output << " outputs=";
							for (std::size_t view{}; view < value.output_view_count; ++view)
							{
								if (view) output << ',';
								output << "0x" << std::hex
									<< value.output_views[view].view << ":0x"
									<< value.output_views[view].resource << std::dec;
							}
							output << " clear(first/last)=0x" << std::hex
								<< value.first_clear_view << ":0x"
								<< value.first_clear_resource << "/0x"
								<< value.last_clear_view << ":0x"
								<< value.last_clear_resource << std::dec << '\n';
						}
					}
				}

				for (std::size_t family{}; family < family_names.size(); ++family)
				{
					const auto& left = sample.eyes[0].dynamic[family];
					const auto& right = sample.eyes[1].dynamic[family];
					if (left.calls == 0 && right.calls == 0) continue;
					output << "      effect_timeline_fx: family=" << family_names[family];
					output << " calls=" << left.calls << '/' << right.calls;
					output << " detailed=" << left.detailed_samples << '/'
						<< right.detailed_samples;
					output << " stream=0x" << std::hex << left.stream_hash << '/'
						<< right.stream_hash;
					output << " pipeline=0x" << left.pipeline_hash << '/'
						<< right.pipeline_hash;
					output << " first_ps=0x" << left.first.pixel_shader << '/'
						<< right.first.pixel_shader;
					output << " first_blend=0x" << left.first.blend_state << '/'
						<< right.first.blend_state;
					output << " first_srv=0x" << left.first.shader_resource_hash << '/'
						<< right.first.shader_resource_hash;
					output << " first_cb=0x" << left.first.constant_buffer_hash << '/'
						<< right.first.constant_buffer_hash << std::dec << '\n';
				}

				const auto emit_invocation = [&output](const char* const eye,
					const engine_stereo_effect_timeline::invocation_fingerprint& value)
				{
					output << ' ' << eye << "={present=" << yes_no(value.present);
					if (value.present)
					{
						output << ",api=" << static_cast<std::uint32_t>(value.operation);
						output << ",caller=0x" << std::hex << value.caller << std::dec;
						output << ",target=" << value.output_target;
						output << ",args=[";
						const auto count = (std::min)(
							static_cast<std::size_t>(value.argument_count),
							value.arguments.size());
						for (std::size_t argument{}; argument < count; ++argument)
						{
							if (argument) output << ',';
							output << value.arguments[argument];
						}
						output << ']';
						output << ",ps/blend=0x" << std::hex << value.pixel_shader
							<< "/0x" << value.blend_state;
						output << ",mask=0x" << value.sample_mask;
						output << ",blend_factor=";
						for (const auto bits : value.blend_factor_bits)
							output << "/0x" << bits;
						output << ",srv(identity/descriptor)=0x"
							<< value.shader_resource_identity_hash << "/0x"
							<< value.shader_resource_descriptor_hash;
						output << ",cb(identity/content/known/unknown)=0x"
							<< value.constant_buffer_identity_hash << "/0x"
							<< value.constant_buffer_content_hash << "/0x"
							<< value.constant_buffer_known_mask << "/0x"
							<< value.constant_buffer_unknown_mask << std::dec;
					}
					output << '}';
				};
				for (std::size_t stream{}; stream < sample.divergences.size(); ++stream)
				{
					const auto& divergence = sample.divergences[stream];
					const auto structural = divergence.observed ||
						divergence.output0_overflow || divergence.output1_overflow;
					if (structural)
					{
						output << "      effect_timeline_divergence: stream="
							<< stream_names[stream];
						output << " observed=" << yes_no(divergence.observed);
						output << " first=" << divergence.first_ordinal;
						output << " alignment=" << alignment_name(divergence.alignment);
						output << " skips=" << static_cast<std::uint32_t>(
							divergence.output0_skip) << '/' << static_cast<std::uint32_t>(
							divergence.output1_skip);
						output << " counts=" << divergence.output0_count << '/'
							<< divergence.output1_count;
						output << " overflow=" << yes_no(divergence.output0_overflow) << '/'
							<< yes_no(divergence.output1_overflow) << '\n';
						for (std::size_t window{}; window <
							engine_stereo_effect_timeline::divergence_window_size; ++window)
						{
							const auto& left = divergence.output0[window];
							const auto& right = divergence.output1[window];
							if (!left.present && !right.present) continue;
							output << "        effect_timeline_divergence_window: stream="
								<< stream_names[stream];
							output << " ordinal=" << divergence.window_first_ordinal + window;
							emit_invocation("output0", left);
							emit_invocation("output1", right);
							output << '\n';
						}
					}

					if (sample.pair_id != effect_timeline_status.frozen_pair) continue;
					const auto& semantics = divergence.semantics;
					output << "      effect_timeline_semantics: stream="
						<< stream_names[stream];
					output << " observed=" << yes_no(semantics.observed);
					output << " aligned/resync=" << semantics.aligned_invocations << '/'
						<< semantics.structural_resyncs;
					output << " state_mismatch(ps/blend/mask/factor)="
						<< semantics.pixel_shader_mismatches << '/'
						<< semantics.blend_state_mismatches << '/'
						<< semantics.sample_mask_mismatches << '/'
						<< semantics.blend_factor_mismatches;
					output << " first=" << semantics.first_output0_ordinal << '/'
						<< semantics.first_output1_ordinal;
					if (semantics.first_output0_ordinal != 0)
					{
						emit_invocation("output0", semantics.first_output0);
						emit_invocation("output1", semantics.first_output1);
					}
					output << '\n';

					for (std::size_t slot{}; slot < semantics.shader_resources.size(); ++slot)
					{
						const auto& difference = semantics.shader_resources[slot];
						if (difference.view_identity_mismatches == 0 &&
							difference.resource_identity_mismatches == 0 &&
							difference.descriptor_mismatches == 0) continue;
						output << "        effect_timeline_semantic_srv: stream="
							<< stream_names[stream] << " slot=" << slot;
						output << " compared=" << difference.comparisons;
						output << " mismatch(view/resource/descriptor)="
							<< difference.view_identity_mismatches << '/'
							<< difference.resource_identity_mismatches << '/'
							<< difference.descriptor_mismatches;
						output << " first=" << difference.first_output0_ordinal << '/'
							<< difference.first_output1_ordinal;
						output << " output0(view/resource/descriptor)=0x" << std::hex
							<< difference.output0.view << "/0x" << difference.output0.resource
							<< "/0x" << difference.output0.descriptor_hash;
						output << " output1(view/resource/descriptor)=0x"
							<< difference.output1.view << "/0x" << difference.output1.resource
							<< "/0x" << difference.output1.descriptor_hash << std::dec << '\n';
					}

					for (std::size_t slot{}; slot < semantics.constant_buffers.size(); ++slot)
					{
						const auto& difference = semantics.constant_buffers[slot];
						if (difference.identity_mismatches == 0 &&
							difference.known_state_mismatches == 0 &&
							difference.content_mismatches == 0) continue;
						output << "        effect_timeline_semantic_cb: stream="
							<< stream_names[stream] << " slot=" << slot;
						output << " compared=" << difference.comparisons;
						output << " mismatch(identity/known/content)="
							<< difference.identity_mismatches << '/'
							<< difference.known_state_mismatches << '/'
							<< difference.content_mismatches;
						output << " content_compared=" << difference.content_comparisons;
						output << " first=" << difference.first_output0_ordinal << '/'
							<< difference.first_output1_ordinal;
						output << " output0(buffer/known/bytes/hash)=0x" << std::hex
							<< difference.output0.buffer << std::dec << '/'
							<< yes_no(difference.output0.known) << '/'
							<< difference.output0.byte_width << "/0x" << std::hex
							<< difference.output0.hash_low << ":0x"
							<< difference.output0.hash_high;
						output << " output1(buffer/known/bytes/hash)=0x"
							<< difference.output1.buffer << std::dec << '/'
							<< yes_no(difference.output1.known) << '/'
							<< difference.output1.byte_width << "/0x" << std::hex
							<< difference.output1.hash_low << ":0x"
							<< difference.output1.hash_high << std::dec << '\n';
					}
				}
			}
		}
	}
}
