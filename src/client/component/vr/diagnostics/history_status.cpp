#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "../engine_stereo_constant_buffer_probe.hpp"
#include "../engine_stereo_dxbc_declarations.hpp"
#include "../engine_stereo_eye_resources.hpp"
#include "../engine_stereo_ssr_consumer_probe.hpp"
#include "../engine_stereo_ssr_consumer_window.hpp"
#include "../engine_stereo_ssr_history_probe.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr::diagnostics::detail
{
	void append_history_status(std::ostringstream& output,
		const engine_stereo_ssr_history_probe::report& ssr_history_status,
		const engine_stereo_ssr_consumer_probe::report& ssr_consumer_status,
		const engine_stereo_eye_resources::status& eye_resource_status)
	{
		output << "  backend_ssr_history: state="
			<< engine_stereo_ssr_history_probe::to_string(ssr_history_status.state);
		output << " attempts=" << ssr_history_status.attempts;
		output << " complete=" << ssr_history_status.completions;
		output << " failures=" << ssr_history_status.failures;
		output << " pair=" << ssr_history_status.pair_id;
		output << " publication=" << ssr_history_status.publication;
		output << " current_vp_equal="
			<< yes_no(ssr_history_status.current_view_projection_equal);
		output << " current_origin_equal="
			<< yes_no(ssr_history_status.current_origin_equal);
		output << " history_vp_equal="
			<< yes_no(ssr_history_status.history_view_projection_equal);
		output << " history_origin_equal="
			<< yes_no(ssr_history_status.history_origin_equal);
		output << " a0_a3_equal="
			<< yes_no(ssr_history_status.previous_view_projection_constants_equal);
		output << " a4_equal="
			<< yes_no(ssr_history_status.previous_eye_position_constants_equal);
		output << " shared_matrix_gap="
			<< yes_no(ssr_history_status.shared_matrix_history_gap);
		output << " per_eye_matrix_history="
			<< yes_no(ssr_history_status.distinct_per_eye_matrix_history) << '\n';
		output << "  backend_ssr_history_origin_contract: record+0x2dc0="
			"post_clone_xmodel_rebase reliable_for_ssr="
			<< yes_no(ssr_history_status.history_origin_reliable_for_ssr) << '\n';
		if (ssr_history_status.state ==
			engine_stereo_ssr_history_probe::capture_state::complete)
		{
			const auto append_values = [&output](const char* const label,
				const auto& values)
			{
				output << label << '=';
				for (std::size_t index{}; index < values.size(); ++index)
				{
					if (index != 0) output << ',';
					output << values[index];
				}
				output << '\n';
			};
			for (std::size_t eye{}; eye < ssr_history_status.eyes.size(); ++eye)
			{
				const auto& sample = ssr_history_status.eyes[eye];
				output << "  backend_ssr_history[" << eye << "]: record=0x"
					<< std::hex << sample.record << std::dec;
				output << " finite=" << yes_no(sample.finite);
				output << " a0_matches_history="
					<< yes_no(sample.previous_view_projection_matches_history);
				output << " a4_matches_raw_delta="
					<< yes_no(sample.previous_eye_position_matches_raw_origin_delta);
				output << " a4_matches_scaled_delta="
					<< yes_no(sample.previous_eye_position_matches_scaled_origin_delta)
					<< '\n';
				std::ostringstream prefix;
				prefix << "    eye" << eye << '.';
				const auto base = prefix.str();
				append_values((base + "current_vp").c_str(),
					sample.current_view_projection);
				append_values((base + "current_origin").c_str(), sample.current_origin);
				append_values((base + "history_vp_record_2d40").c_str(),
					sample.history_view_projection);
				append_values((base + "history_origin_record_2dc0_unreliable").c_str(),
					sample.history_origin);
				append_values((base + "a0_a3_record_f90").c_str(),
					sample.previous_view_projection_constant);
				append_values((base + "a4_record_fd0").c_str(),
					sample.previous_eye_position_constant);
				append_values((base + "current_minus_record_history_origin").c_str(),
					sample.origin_delta);
			}
		}
		const auto& ssr_window = ssr_consumer_status.window;
		output << "  backend_scene_mip_consumer_discovery: state="
			<< engine_stereo_ssr_consumer_window::to_string(ssr_window.current);
		output << " error="
			<< engine_stereo_ssr_consumer_window::to_string(ssr_window.error);
		output << " installed/attached/content_tracking="
			<< yes_no(ssr_consumer_status.installed) << '/'
			<< yes_no(ssr_consumer_status.observer_attached) << '/'
			<< yes_no(ssr_consumer_status.content_tracking_active);
		output << " context=0x" << std::hex << ssr_consumer_status.expected_context
			<< std::dec << " generation=" << ssr_consumer_status.device_generation;
		output << " active(pair/eye/thread)=" << ssr_consumer_status.active_pair
			<< '/' << ssr_consumer_status.active_eye << '/'
			<< ssr_consumer_status.owner_thread;
		output << " pairs(success/failed/seed/sample)="
			<< ssr_window.successful_pairs << '/' << ssr_window.unsuccessful_pairs
			<< '/' << ssr_window.skipped_seed_pairs << '/' << ssr_window.sampled_pairs;
		output << " eye_sample_pairs=" << ssr_window.sampled_eye_pairs[0]
			<< '/' << ssr_window.sampled_eye_pairs[1];
		output << " candidate_bytecode_key=0x" << std::hex
			<< ssr_window.candidate_shader
			<< std::dec << '\n';
		const auto& ssr_focused = ssr_consumer_status.focused;
		output << "    scene_mip_focused_consumer: state="
			<< engine_stereo_ssr_consumer_window::to_string(ssr_focused.current)
			<< " error="
			<< engine_stereo_ssr_consumer_window::to_string(ssr_focused.error)
			<< " candidate=0x" << std::hex << ssr_focused.candidate_shader
			<< std::dec << " attempts=" << ssr_focused.attempts
			<< " skipped_seed=" << ssr_focused.skipped_seed_pairs
			<< " active_pair=" << ssr_focused.active_pair
			<< " eye_masks(completed/sample/content_complete)=0x" << std::hex
			<< static_cast<std::uint32_t>(ssr_focused.completed_eye_mask) << "/0x"
			<< static_cast<std::uint32_t>(ssr_focused.sample_eye_mask) << "/0x"
			<< static_cast<std::uint32_t>(ssr_focused.content_complete_eye_mask)
			<< std::dec << '\n';
		output << "    scene_mip_candidate_pair_counts: entries="
			<< ssr_window.cross_pair_candidate_count;
		for (std::size_t index{}; index < ssr_window.cross_pair_candidate_count;
			++index)
		{
			output << " [0x" << std::hex << ssr_window.cross_pair_candidates[index]
				<< std::dec << ':' << ssr_window.cross_pair_candidate_pairs[index] << ']';
		}
		output << '\n';
		output << "    scene_mip_candidate_filter: caller=0x" << std::hex
			<< engine_stereo_ssr_consumer_probe::exact_consumer_caller << std::dec;
		output << " target="
			<< engine_stereo_ssr_consumer_probe::exact_consumer_target;
		output << " required_ps_srv_slot="
			<< engine_stereo_ssr_consumer_probe::scene_mip_srv_slot;
		output << " exact_entries=" << ssr_consumer_status.callback_entries;
		output << " rejected(inactive/target/context/thread/eye)="
			<< ssr_consumer_status.inactive_rejections << '/'
			<< ssr_consumer_status.target_rejections << '/'
			<< ssr_consumer_status.context_rejections << '/'
			<< ssr_consumer_status.thread_rejections << '/'
			<< ssr_consumer_status.eye_rejections << '\n';
		output << "    scene_mip_candidate_reflection: shader_queries="
			<< ssr_consumer_status.shader_queries;
		output << " shader_missing=" << ssr_consumer_status.shader_missing;
		output << " bytecode(missing/oversized)="
			<< ssr_consumer_status.bytecode_missing << '/'
			<< ssr_consumer_status.bytecode_oversized;
		output << " reflection(fail/empty/cache_overflow)="
			<< ssr_consumer_status.reflection_failures << '/'
			<< ssr_consumer_status.reflection_empty << '/'
			<< ssr_consumer_status.reflection_cache_overflows;
		output << " disassembly(attempts/fail/unsupported)="
			<< ssr_consumer_status.disassembly_attempts << '/'
			<< ssr_consumer_status.disassembly_failures << '/'
			<< ssr_consumer_status.disassembly_unsupported_profiles;
		output << " recovered(srv/cb/out_of_range/malformed)="
			<< ssr_consumer_status.disassembly_recovered_srv_bindings << '/'
			<< ssr_consumer_status.disassembly_recovered_constant_buffer_bindings << '/'
			<< ssr_consumer_status.disassembly_out_of_range_declarations << '/'
			<< ssr_consumer_status.disassembly_malformed_declarations;
		output << " declared(srv/cb)=" << ssr_consumer_status.declared_srv_bindings
			<< '/' << ssr_consumer_status.declared_constant_buffer_bindings;
		output << " direct_bound(srv/cb)="
			<< ssr_consumer_status.direct_bound_srv_bindings << '/'
			<< ssr_consumer_status.direct_bound_constant_buffer_bindings;
		output << " scene_mip(rejected_decl/binding/resource/candidate)="
			<< ssr_consumer_status.scene_mip_declaration_rejections << '/'
			<< ssr_consumer_status.scene_mip_binding_rejections << '/'
			<< ssr_consumer_status.scene_mip_resource_rejections << '/'
			<< ssr_consumer_status.scene_mip_candidates;
		output << " ssr_name_rejections="
			<< ssr_consumer_status.ssr_name_rejections;
		output << " cb_content(known/unknown)=" << ssr_consumer_status.content_known
			<< '/' << ssr_consumer_status.content_unknown;
		output << " cb_bytes(known/unavailable)="
			<< ssr_consumer_status.content_bytes_known << '/'
			<< ssr_consumer_status.content_bytes_unavailable;
		output << " samples=" << ssr_consumer_status.sample_count
			<< " overflow=" << ssr_consumer_status.sample_overflows << '\n';
		output << "    scene_mip_resource_write_provenance: semantics="
			"resource_level_cpu_submission_before_selected_draw";
		output << " callbacks/rejected/records/overflow/opaque_barriers/native_conversion_lists="
			<< ssr_consumer_status.resource_write_callbacks << '/'
			<< ssr_consumer_status.resource_write_rejections << '/'
			<< ssr_consumer_status.resource_write_records << '/'
			<< ssr_consumer_status.resource_write_overflows << '/'
			<< ssr_consumer_status.resource_write_opaque_barriers << '/'
			<< ssr_consumer_status.resource_write_native_conversion_lists;
		output << " declared_srv_last_write(known/unknown)="
			<< ssr_consumer_status.declared_srv_last_write_known << '/'
			<< ssr_consumer_status.declared_srv_last_write_unknown << '\n';
		output << "    scene_mip_copy_source_lineage: candidates/known/unknown/overflow="
			<< ssr_consumer_status.copy_source_lineage_candidates << '/'
			<< ssr_consumer_status.copy_source_lineage_known << '/'
			<< ssr_consumer_status.copy_source_lineage_unknown << '/'
			<< ssr_consumer_status.copy_source_lineage_overflows << '\n';
		for (std::size_t sample_index{};
			sample_index < ssr_consumer_status.copy_source_lineage_sample_count;
			++sample_index)
		{
			const auto& sample =
				ssr_consumer_status.copy_source_lineage_samples[sample_index];
			const auto& copy = sample.lineage.copy;
			const auto& producer = sample.lineage.source_last_write;
			output << "      copy_source_lineage_sample[" << sample_index << "]: pair="
				<< copy.pair_id << " eye=" << copy.eye;
			output << " dst/src=0x" << std::hex << copy.destination << "/0x"
				<< copy.source << " caller=0x" << copy.caller << std::dec;
			output << " source_last_write="
				<< yes_no(sample.lineage.source_last_write_known);
			output << " desc(dst_bind/src_bind/src_misc/src_stride)=0x" << std::hex
				<< sample.destination_descriptor.bind_flags << "/0x"
				<< sample.source_descriptor.bind_flags << "/0x"
				<< sample.source_descriptor.misc_flags << std::dec << '/'
				<< sample.source_descriptor.structure_byte_stride;
			if (sample.lineage.source_last_write_known)
			{
				output << " producer(sequence/pair/eye/operation/target)="
					<< producer.sequence << '/' << producer.pair_id << '/'
					<< producer.eye << '/'
					<< engine_stereo_ssr_consumer_window::to_string(
						producer.operation) << '/' << producer.output_target;
				output << " producer(destination/caller)=0x" << std::hex
					<< producer.destination << "/0x" << producer.caller << std::dec;
			}
			output << '\n';
		}
		for (std::size_t sample_index{};
			sample_index < ssr_consumer_status.sample_count; ++sample_index)
		{
			const auto& sample = ssr_consumer_status.samples[sample_index];
			output << "    scene_mip_candidate_sample[" << sample_index << "]: pair="
				<< sample.pair_id << " eye=" << sample.eye;
			output << " target=" << sample.output_target;
			output << " caller=0x" << std::hex << sample.caller;
			output << " ps=0x" << sample.shader << std::dec;
			output << " bytecode_key=0x" << std::hex << sample.bytecode_hash
				<< std::dec;
			output << " debug_name=\"" << sample.shader_debug_name.data() << "\"";
			output << " debug_hash=0x" << std::hex
				<< sample.shader_debug_name_hash << std::dec;
			output << " reflected/disassembled/declarations="
				<< yes_no(sample.reflection_resolved) << '/'
				<< yes_no(sample.disassembly_resolved) << '/'
				<< yes_no(sample.declarations_resolved);
			output << " profile=" << engine_stereo_dxbc_declarations::to_string(
				sample.shader_profile);
			output << " ssr_name=" << yes_no(sample.ssr_name_match);
			output << " candidate=" << yes_no(sample.scene_mip_candidate);
			output << " declared/bound(srv/cb)=" << sample.declared_srv_count
				<< '/' << sample.bound_srv_count << '/'
				<< sample.declared_constant_buffer_count << '/'
				<< sample.bound_constant_buffer_count << '\n';
			for (const auto& srv : sample.shader_resources)
			{
				if (!srv.declaration.declared && srv.view == 0) continue;
				output << "      scene_mip_candidate_ps_srv: slot=" << srv.slot;
				output << " declared=" << yes_no(srv.declaration.declared);
				output << " source=" << (srv.declaration.declared_in_reflection &&
					srv.declaration.declared_in_disassembly ? "reflection+disassembly" :
					srv.declaration.declared_in_disassembly ? "disassembly" :
					srv.declaration.declared_in_reflection ? "reflection" : "direct");
				output << " declaration_sources(reflection/disassembly)="
					<< yes_no(srv.declaration.declared_in_reflection) << '/'
					<< yes_no(srv.declaration.declared_in_disassembly);
				output << " name=\"" << srv.declaration.name.data() << "\"";
				output << " name_hash=0x" << std::hex
					<< srv.declaration.name_hash << std::dec;
				output << " declared(type/return/dimension/bind)="
					<< srv.declaration.input_type << '/'
					<< srv.declaration.return_type << '/'
					<< srv.declaration.dimension << '/'
					<< srv.declaration.bind_point << '+'
					<< srv.declaration.bind_count;
				output << " view/resource=0x" << std::hex << srv.view << "/0x"
					<< srv.resource << std::dec;
				output << " view(format/dimension/mip/array/elements)="
					<< srv.view_descriptor.format << '/'
					<< srv.view_descriptor.dimension << '/'
					<< srv.view_descriptor.most_detailed_mip << '+'
					<< srv.view_descriptor.mip_levels << '/'
					<< srv.view_descriptor.first_array_slice << '+'
					<< srv.view_descriptor.array_size << '/'
					<< srv.view_descriptor.first_element << '+'
					<< srv.view_descriptor.element_count;
				const auto& resource = srv.resource_descriptor;
				output << " resource(valid/dimension/size/mip/array/format/samples/bind/misc)="
					<< yes_no(resource.valid) << '/' << resource.dimension << '/'
					<< resource.width << 'x' << resource.height << 'x' << resource.depth
					<< '/' << resource.mip_levels << '/' << resource.array_size << '/'
					<< resource.format << '/' << resource.sample_count << '+'
					<< resource.sample_quality << "/0x" << std::hex
					<< resource.bind_flags << "/0x" << resource.misc_flags << std::dec;
				const auto& last_write = srv.last_write;
				output << " last_write(known/sequence/pair/eye/operation)="
					<< yes_no(static_cast<bool>(last_write)) << '/'
					<< last_write.sequence << '/' << last_write.pair_id << '/'
					<< last_write.eye << '/'
					<< engine_stereo_ssr_consumer_window::to_string(
						last_write.operation);
				output << " last_write(context/destination/subresource/source/"
					"source_subresource/output_target/caller/thread)=0x" << std::hex
					<< last_write.context << "/0x" << last_write.destination << '/'
					<< std::dec << last_write.destination_subresource << "/0x"
					<< std::hex << last_write.source << '/' << std::dec
					<< last_write.source_subresource << '/' << last_write.output_target
					<< "/0x" << std::hex
					<< last_write.caller << std::dec << '/' << last_write.thread_id
					<< '\n';
			}
			for (const auto& buffer : sample.constant_buffers)
			{
				if (!buffer.declaration.declared && buffer.buffer == 0) continue;
				output << "      scene_mip_candidate_ps_cb: slot=" << buffer.slot;
				output << " declared=" << yes_no(buffer.declaration.declared);
				output << " source=" << (buffer.declaration.declared_in_reflection &&
					buffer.declaration.declared_in_disassembly ? "reflection+disassembly" :
					buffer.declaration.declared_in_disassembly ? "disassembly" :
					buffer.declaration.declared_in_reflection ? "reflection" : "direct");
				output << " declaration_sources(reflection/disassembly)="
					<< yes_no(buffer.declaration.declared_in_reflection) << '/'
					<< yes_no(buffer.declaration.declared_in_disassembly);
				output << " name=\"" << buffer.declaration.name.data() << "\"";
				output << " name_hash=0x" << std::hex
					<< buffer.declaration.name_hash << std::dec;
				output << " bind=" << buffer.declaration.bind_point << '+'
					<< buffer.declaration.bind_count;
				output << " buffer=0x" << std::hex << buffer.buffer << std::dec;
				output << " bytes=" << buffer.byte_width;
				output << " content(known/generation/hash/source/caller/thread)="
					<< yes_no(buffer.content.known) << '/'
					<< buffer.content.upload_generation << "/0x" << std::hex
					<< buffer.content.hash_low << ":0x" << buffer.content.hash_high
					<< '/' << std::dec << engine_stereo_constant_buffer_probe::to_string(
						buffer.content.source) << "/0x" << std::hex
					<< buffer.content.upload_caller << std::dec << '/'
					<< buffer.content.upload_thread;
				output << " byte_snapshot(available/captured/complete)="
					<< yes_no(buffer.content_bytes_available) << '/'
					<< buffer.content_bytes.captured_bytes << '/'
					<< yes_no(buffer.content_bytes.complete) << '\n';
			}
		}
		for (std::size_t left_index{}; left_index < ssr_consumer_status.sample_count;
			++left_index)
		{
			const auto& left = ssr_consumer_status.samples[left_index];
			if (left.eye != 0 || !left.scene_mip_candidate) continue;
			const engine_stereo_ssr_consumer_probe::consumer_sample* right{};
			for (std::size_t right_index{};
				right_index < ssr_consumer_status.sample_count; ++right_index)
			{
				const auto& candidate = ssr_consumer_status.samples[right_index];
				if (candidate.eye == 1 && candidate.scene_mip_candidate &&
					candidate.pair_id == left.pair_id &&
					candidate.bytecode_hash == left.bytecode_hash)
				{
					right = &candidate;
					break;
				}
			}
			if (right == nullptr) continue;
			for (std::size_t slot{}; slot < left.constant_buffers.size(); ++slot)
			{
				const auto& left_buffer = left.constant_buffers[slot];
				const auto& right_buffer = right->constant_buffers[slot];
				if (left_buffer.buffer == 0 && right_buffer.buffer == 0) continue;
				const auto bytes_available = left_buffer.content_bytes_available &&
					right_buffer.content_bytes_available;
				const auto compared = bytes_available ? (std::min)(
					left_buffer.content_bytes.captured_bytes,
					right_buffer.content_bytes.captured_bytes) : 0u;
				std::uint32_t different{}, first{}, last{};
				std::array<std::uint16_t, 32> difference_offsets{};
				std::size_t difference_sample_count{};
				for (std::uint32_t byte{}; byte < compared; ++byte)
				{
					if (left_buffer.content_bytes.bytes[byte] ==
						right_buffer.content_bytes.bytes[byte]) continue;
					if (different == 0) first = byte;
					last = byte;
					++different;
					if (difference_sample_count < difference_offsets.size())
						difference_offsets[difference_sample_count++] =
							static_cast<std::uint16_t>(byte);
				}
				struct word_difference
				{
					std::uint16_t offset{};
					std::uint32_t output0{}, output1{};
				};
				std::array<word_difference, 64> word_differences{};
				std::size_t word_difference_sample_count{};
				std::uint32_t differing_words{}, first_word{}, last_word{};
				for (std::uint32_t word{};
					word < compared / static_cast<std::uint32_t>(sizeof(std::uint32_t));
					++word)
				{
					const auto offset = word * static_cast<std::uint32_t>(
						sizeof(std::uint32_t));
					std::uint32_t left_bits{};
					std::uint32_t right_bits{};
					std::memcpy(&left_bits,
						left_buffer.content_bytes.bytes.data() + offset,
						sizeof(left_bits));
					std::memcpy(&right_bits,
						right_buffer.content_bytes.bytes.data() + offset,
						sizeof(right_bits));
					if (left_bits == right_bits) continue;
					if (differing_words == 0) first_word = offset;
					last_word = offset;
					++differing_words;
					if (word_difference_sample_count < word_differences.size())
					{
						auto& destination =
							word_differences[word_difference_sample_count++];
						destination.offset = static_cast<std::uint16_t>(offset);
						destination.output0 = left_bits;
						destination.output1 = right_bits;
					}
				}
				output << "    scene_mip_candidate_ps_cb_pair: pair=" << left.pair_id;
				output << " bytecode_key=0x" << std::hex << left.bytecode_hash
					<< std::dec;
				output << " slot=" << slot;
				output << " identity_equal=" << yes_no(left_buffer.buffer != 0 &&
					left_buffer.buffer == right_buffer.buffer);
				output << " hash_equal=" << yes_no(left_buffer.content.known &&
					right_buffer.content.known &&
					left_buffer.content.hash_low == right_buffer.content.hash_low &&
					left_buffer.content.hash_high == right_buffer.content.hash_high);
				output << " byte_diff(available/compared/different/first/last)="
					<< yes_no(bytes_available) << '/' << compared << '/' << different
					<< '/' << first << '/' << last;
				output << " offsets=[";
				for (std::size_t index{}; index < difference_sample_count; ++index)
				{
					if (index) output << ',';
					output << difference_offsets[index];
				}
				output << ']';
				output << " word_diff(compared/different/first/last)="
					<< compared / static_cast<std::uint32_t>(sizeof(std::uint32_t))
					<< '/' << differing_words << '/' << first_word << '/' << last_word;
				output << " words=[";
				for (std::size_t index{}; index < word_difference_sample_count; ++index)
				{
					if (index) output << ',';
					const auto& word = word_differences[index];
					output << word.offset << ":0x" << std::hex << word.output0
						<< "->0x" << word.output1 << std::dec << '('
						<< float_from_bits(word.output0) << "->"
						<< float_from_bits(word.output1) << ')';
				}
				output << "]\n";
			}
		}
		output << "  backend_eye_resource_diagnostic: production=active mode=target91_persistent_both_before_left hooks="
			<< (eye_resource_status.hooks_installed ? "yes" : "no");
		output << " ready=" << (eye_resource_status.resources_ready ? "yes" : "no");
		output << " active=" << (eye_resource_status.pair_active ? "yes" : "no");
		output << " failed=" << (eye_resource_status.pair_failed ? "yes" : "no");
		output << " quarantined="
			<< (eye_resource_status.cleanup_quarantined ? "yes" : "no");
		output << " invalidation_pending="
			<< (eye_resource_status.invalidation_pending ? "yes" : "no");
		output << " error=" << engine_stereo_eye_resources::to_string(
			eye_resource_status.last_failure);
		output << " context=0x" << std::hex << eye_resource_status.expected_context;
		output << " generation=" << std::dec << eye_resource_status.device_generation;
		output << " pair=" << eye_resource_status.active_pair;
		output << " thread=" << eye_resource_status.owner_thread;
		output << " eye=" << eye_resource_status.active_eye;
		output << " attempts=" << eye_resource_status.pair_attempts;
		output << " complete=" << eye_resource_status.pair_completions;
		output << " failures=" << eye_resource_status.pair_failures;
		output << " quarantine=" << eye_resource_status.quarantine_events
			<< '/' << eye_resource_status.quarantine_rejections;
		output << " cleanup=" << eye_resource_status.cleanup_retries
			<< '/' << eye_resource_status.cleanup_recoveries;
		output << " deferred_invalidations="
			<< eye_resource_status.deferred_invalidations;
		output << " rebuilds=" << eye_resource_status.resource_rebuilds;
		output << " seeds=" << eye_resource_status.seed_copies;
		output << " views=" << eye_resource_status.view_creations;
		output << " capacity_failures=" << eye_resource_status.view_capacity_failures;
		output << " boundary=" << eye_resource_status.boundary_rebind_attempts
			<< '/' << eye_resource_status.boundary_rebind_completions
			<< '/' << eye_resource_status.boundary_rebind_failures;
		output << " boundary_calls=" << eye_resource_status.boundary_rebind_calls;
		output << " originals_remaining="
			<< eye_resource_status.boundary_original_views_remaining;
		output << " restore=" << eye_resource_status.restore_rebind_attempts
			<< '/' << eye_resource_status.restore_rebind_completions
			<< '/' << eye_resource_status.restore_rebind_failures;
		output << " restore_calls=" << eye_resource_status.restore_rebind_calls;
		output << " isolated_remaining="
			<< eye_resource_status.restore_isolated_views_remaining;
		output << " replacements(resource/rtv/dsv/uav)="
			<< eye_resource_status.resource_replacements << '/'
			<< eye_resource_status.render_target_replacements << '/'
			<< eye_resource_status.depth_stencil_replacements << '/'
			<< eye_resource_status.unordered_access_replacements << '\n';
		if (eye_resource_status.cleanup_quarantined)
		{
			output << "    quarantine_identity: context=0x" << std::hex
				<< eye_resource_status.quarantine_context << std::dec
				<< " generation=" << eye_resource_status.quarantine_generation
				<< " pair=" << eye_resource_status.quarantine_pair
				<< " owner_thread=" << eye_resource_status.quarantine_owner_thread
				<< '\n';
		}
		constexpr std::array<const char*,
			engine_stereo_eye_resources::shader_stage_count> shader_stage_names{
			"PS", "VS", "GS", "HS", "DS", "CS",
		};
		output << "  backend_eye_resource_shader_replacements:";
		for (std::size_t stage{}; stage < shader_stage_names.size(); ++stage)
		{
			output << ' ' << shader_stage_names[stage] << '='
				<< eye_resource_status.shader_resource_replacements[stage]
				<< "@0x" << std::hex << eye_resource_status.shader_hook_targets[stage]
				<< std::dec;
		}
		output << " UAV@0x" << std::hex
			<< eye_resource_status.unordered_access_hook_target << std::dec << '\n';
		output << "  backend_eye_resource_exact_read: enabled="
			<< (eye_resource_status.exact_read_enabled ? "yes" : "no");
		output << " pair=" << eye_resource_status.exact_read_pair;
		output << " observations=" << eye_resource_status.exact_read_observations;
		output << " expected=" << eye_resource_status.exact_read_expected;
		output << " cross_eye=" << eye_resource_status.exact_read_unexpected;
		for (std::size_t eye{}; eye < eye_resource_status.exact_read_bindings.size();
			++eye)
		{
			output << " eye" << eye << "(null/natural/left/right/foreign)=";
			for (std::size_t binding{};
				binding < eye_resource_status.exact_read_bindings[eye].size(); ++binding)
			{
				if (binding != 0) output << '/';
				output << eye_resource_status.exact_read_bindings[eye][binding];
			}
		}
		output << " last=" << eye_resource_status.exact_read_last_pair << ':'
			<< eye_resource_status.exact_read_last_eye;
		output << " target=" << eye_resource_status.exact_read_last_output_target;
		output << " binding=" << engine_stereo_eye_resources::to_string(
			eye_resource_status.exact_read_last_binding);
		output << " caller=0x" << std::hex
			<< eye_resource_status.exact_read_last_caller;
		output << " view=0x" << eye_resource_status.exact_read_last_view;
		output << " resource=0x" << eye_resource_status.exact_read_last_resource
			<< std::dec;
		output << " desc=" << eye_resource_status.exact_read_last_view_dimension
			<< '/' << eye_resource_status.exact_read_last_format;
		output << " mip=" << eye_resource_status.exact_read_last_most_detailed_mip
			<< '+' << eye_resource_status.exact_read_last_mip_levels;
		output << " array=" << eye_resource_status.exact_read_last_first_array_slice
			<< '+' << eye_resource_status.exact_read_last_array_size << '\n';
		for (const auto& target : eye_resource_status.targets)
		{
			if (target.original_resource == 0) continue;
			output << "    eye_resource_target: id=" << target.target_id;
			output << " role=" << engine_stereo_eye_resources::to_string(
				target.target_role);
			output << " view=0x" << std::hex << target.owner_view;
			output << " original=0x" << target.original_resource;
			output << " left=0x" << target.left_resource;
			output << " right=0x" << target.right_resource << std::dec;
			output << " size=" << target.width << 'x' << target.height;
			output << " mips=" << target.mip_levels;
			output << " format=" << target.format;
			output << " bind=0x" << std::hex << target.bind_flags << std::dec << '\n';
		}
	}
}
