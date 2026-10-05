#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "../engine_stereo_dynamic_upload.hpp"
#include "../engine_stereo_owner_pass.hpp"
#include "../engine_stereo_scene_batch_probe.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr::diagnostics::detail
{
	void append_scene_status(std::ostringstream& output,
		const engine_stereo_owner_pass::report& owner_pass_status,
		const engine_stereo_scene_batch_probe::report& scene_batch_status)
	{
		output << "  backend_scene_batch_census: state="
			<< engine_stereo_scene_batch_probe::to_string(scene_batch_status.current);
		output << " pair=" << scene_batch_status.pair_id;
		output << " owner_thread=" << scene_batch_status.owner_thread;
		output << " active_eye=" << scene_batch_status.active_eye;
		output << " completed_mask=0x" << std::hex
			<< static_cast<std::uint32_t>(scene_batch_status.completed_eye_mask)
			<< std::dec;
		output << " owner_complete=" << yes_no(scene_batch_status.owner_complete) << '\n';
		output << "    census_clock: utc_ms(arm/start/end)=" << scene_batch_status.armed_utc_ms
			<< '/' << scene_batch_status.started_utc_ms << '/' << scene_batch_status.completed_utc_ms
			<< " us(arm/pair/eye0/eye1/observer0/observer1)=" << scene_batch_status.arm_ns / 1000
			<< '/' << scene_batch_status.pair_wall_ns / 1000
			<< '/' << scene_batch_status.eye_wall_ns[0] / 1000
			<< '/' << scene_batch_status.eye_wall_ns[1] / 1000
			<< '/' << scene_batch_status.eye_observer_ns[0] / 1000
			<< '/' << scene_batch_status.eye_observer_ns[1] / 1000 << '\n';
		for (std::size_t index{}; index < scene_batch_status.observer_timings.size(); ++index)
		{
			const auto& census_timing = scene_batch_status.observer_timings[index];
			output << "    census_observer=" << engine_stereo_scene_batch_probe::to_string(
				static_cast<engine_stereo_scene_batch_probe::observer_stage>(index))
				<< " calls/total_us/max_us=" << census_timing.calls << '/'
				<< census_timing.total_ns / 1000 << '/' << census_timing.maximum_ns / 1000 << '\n';
		}
		const auto print_nested_timing = [&output](const char* name,
			const engine_stereo_scene_batch_probe::timing_sample& census_timing)
		{
			output << "    census_nested=" << name << " calls/total_us/max_us="
				<< census_timing.calls << '/' << census_timing.total_ns / 1000 << '/'
				<< census_timing.maximum_ns / 1000 << '\n';
		};
		print_nested_timing("mutex_wait", scene_batch_status.observer_lock_wait);
		print_nested_timing("guarded_read", scene_batch_status.guarded_read_timing);
		print_nested_timing("virtual_query", scene_batch_status.virtual_query_timing);
		output << "    material_selection: stage=post_gate_not_gpu_binding compared="
			<< scene_batch_status.selection_comparisons
			<< " unreadable=" << scene_batch_status.selection_unreadable
			<< " identity_changed=" << scene_batch_status.selection_identity_mismatches
			<< " pass_bytes_changed=" << scene_batch_status.selection_pass_mismatches
			<< " atlas_changed=" << scene_batch_status.selection_atlas_mismatches
			<< " state_bits(compared/unreadable/changed)=" << scene_batch_status.selection_state_comparisons
			<< '/' << scene_batch_status.selection_state_unreadable << '/' << scene_batch_status.selection_state_mismatches
			<< " per_family_detail_cap=" << engine_stereo_scene_batch_probe::maximum_gate_samples_per_consumer
			<< " names=bounded_63_chars latest_gate=latest_sampled\n";
		output << "    lifecycle: arms=" << scene_batch_status.arm_attempts;
		output << " pairs=" << scene_batch_status.pair_attempts;
		output << " complete=" << scene_batch_status.pair_completions;
		output << " mismatches=" << scene_batch_status.lifecycle_mismatches << '\n';
		output << "    boundaries: observations="
			<< scene_batch_status.observations[0] << '/'
			<< scene_batch_status.observations[1];
		output << " unique=" << scene_batch_status.unique_boundaries[0] << '/'
			<< scene_batch_status.unique_boundaries[1];
		output << " compared=" << scene_batch_status.compared_boundaries;
		output << " missing=" << scene_batch_status.boundary_mismatches;
		output << " duplicate=" << scene_batch_status.boundary_duplicates;
		output << " overflow=" << scene_batch_status.boundary_overflows << '\n';
		output << "    descriptors: compared="
			<< scene_batch_status.compared_descriptors;
		output << " changed=" << scene_batch_status.descriptor_mismatches;
		output << " count_changed="
			<< scene_batch_status.descriptor_count_mismatches;
		output << " backend_data_changed="
			<< scene_batch_status.backend_data_mismatches;
		output << " latest_count=" << scene_batch_status.latest_descriptor_counts[0]
			<< '/' << scene_batch_status.latest_descriptor_counts[1] << '\n';
		output << "    streams: compared=" << scene_batch_status.compared_streams;
		output << " pointer_changed="
			<< scene_batch_status.stream_pointer_mismatches;
		output << " count_changed=" << scene_batch_status.stream_count_mismatches;
		output << " content_changed="
			<< scene_batch_status.stream_content_mismatches;
		output << " unreadable=" << scene_batch_status.unreadable_sources;
		output << " sampled_bytes=" << scene_batch_status.sampled_payload_bytes << '\n';
		output << "    identities: records=0x" << std::hex
			<< scene_batch_status.owner_records[0] << "/0x"
			<< scene_batch_status.owner_records[1];
		output << " backend_states=0x" << scene_batch_status.backend_states[0]
			<< "/0x" << scene_batch_status.backend_states[1];
		output << " backend_data=0x" << scene_batch_status.latest_backend_data[0]
			<< "/0x" << scene_batch_status.latest_backend_data[1]
			<< std::dec << '\n';
		output << "    local_hooks: installed="
			<< yes_no(scene_batch_status.hooks_installed);
		output << " attempts=" << scene_batch_status.hook_install_attempts;
		output << " failures=" << scene_batch_status.hook_install_failures;
		for (std::size_t index{}; index < scene_batch_status.hook_targets.size();
			++index)
		{
			output << " hook" << index << "=0x" << std::hex
				<< scene_batch_status.hook_targets[index] << std::dec;
			output << ":observed="
				<< yes_no(scene_batch_status.hook_callsites_observed[index]);
		}
		output << '\n';
		output << "    local_primary: calls="
			<< scene_batch_status.primary_calls[0] << '/'
			<< scene_batch_status.primary_calls[1];
		output << " descriptor_mutations="
			<< scene_batch_status.primary_source_mutations[0] << '/'
			<< scene_batch_status.primary_source_mutations[1];
		output << " pair16_mutations="
			<< scene_batch_status.primary_pair16_mutations[0] << '/'
			<< scene_batch_status.primary_pair16_mutations[1] << '\n';
		output << "    local_executors: complex_calls="
			<< scene_batch_status.complex_executor_calls[0] << '/'
			<< scene_batch_status.complex_executor_calls[1];
		output << " simple_calls=" << scene_batch_status.simple_executor_calls[0]
			<< '/' << scene_batch_status.simple_executor_calls[1];
		output << " complex_groups=" << scene_batch_status.complex_group_counts[0]
			<< '/' << scene_batch_status.complex_group_counts[1];
		output << " complex_entries=" << scene_batch_status.complex_entry_counts[0]
			<< '/' << scene_batch_status.complex_entry_counts[1];
		output << " simple_entries=" << scene_batch_status.simple_entry_counts[0]
			<< '/' << scene_batch_status.simple_entry_counts[1];
		output << " parse_failures=" << scene_batch_status.local_parse_failures;
		output << " capacity_overflows="
			<< scene_batch_status.local_capacity_overflows;
		output << " lifecycle_mismatches="
			<< scene_batch_status.local_lifecycle_mismatches << '\n';
		output << "    local_cross_eye: payload_compared="
			<< scene_batch_status.local_payload_comparisons;
		output << " payload_changed="
			<< scene_batch_status.local_payload_mismatches;
		output << " payload_unreadable="
			<< scene_batch_status.local_payload_unreadable;
		output << " payload_pair_miss="
			<< scene_batch_status.local_payload_pair_misses;
		output << " pointer_changed="
			<< scene_batch_status.local_source_pointer_mismatches;
		output << " count_changed="
			<< scene_batch_status.local_source_count_mismatches;
		output << " gate_compared=" << scene_batch_status.local_gate_comparisons;
		output << " gate_result_changed="
			<< scene_batch_status.local_gate_result_mismatches;
		output << " gate_key_changed="
			<< scene_batch_status.local_gate_key_mismatches;
		output << " gate_pass_count_changed="
			<< scene_batch_status.local_gate_pass_count_mismatches;
		output << " gate_context_unreadable="
			<< scene_batch_status.local_gate_context_unreadable;
		output << " gate_pair_miss="
			<< scene_batch_status.local_gate_pair_misses << '\n';
		for (const auto& consumer : scene_batch_status.local_consumers)
		{
			output << "    local_consumer=";
			if (consumer.consumer == engine_stereo_scene_batch_probe::ssr_consumer)
				output << "ssr";
			else if (consumer.consumer ==
				engine_stereo_scene_batch_probe::code_trans_consumer)
				output << "code_trans";
			else if (consumer.consumer ==
				engine_stereo_scene_batch_probe::glass_consumer)
				output << "glass";
			else if (consumer.consumer ==
				engine_stereo_scene_batch_probe::spark_consumer)
				output << "spark";
			else
				output << "unknown";
			output << " function=0x" << std::hex << consumer.consumer << std::dec;
			for (std::uint32_t eye{}; eye < 2; ++eye)
			{
				const auto& value = consumer.eyes[eye];
				output << " eye" << eye << "[calls=" << value.calls
					<< " entries=" << value.entries
					<< " advance=" << value.cursor_advances
					<< " unchanged=" << value.cursor_unchanged
					<< " regress=" << value.cursor_regressions
					<< " advance_bytes=" << value.cursor_advance_bytes
					<< " unresolved=" << value.unresolved_sources
					<< " shape_mismatch=" << value.source_shape_mismatches
					<< " hashes=" << value.payload_hashes
					<< " hash_fail=" << value.payload_hash_failures
					<< " hash_bytes=" << value.payload_sampled_bytes
					<< " gate=" << value.gate_calls
					<< ':' << value.gate_true << '/' << value.gate_false
					<< " gate_context_fail=" << value.gate_context_failures
					<< " gate_detail=" << value.gate_samples << '/' << value.gate_samples_dropped
					<< " second_pass=" << value.second_pass_calls
					<< " source=0x" << std::hex << value.latest_source_descriptor
					<< " pointer=0x" << value.latest_source_pointer
					<< " hash=0x" << value.latest_source_content_hash
					<< " cursor=0x" << value.latest_cursor_pre
					<< "->0x" << value.latest_cursor_post
					<< "/0x" << value.latest_cursor_end
					<< " gate_context=0x" << value.latest_gate_context
					<< " secondary=0x" << value.latest_gate_secondary
					<< " technique=0x" << value.latest_gate_technique
					<< " gate_key=0x" << value.latest_gate_key << std::dec
					<< " count=" << value.latest_source_count
					<< " bytes=" << value.latest_source_byte_count
					<< " sampled=" << value.latest_source_sampled_bytes
					<< '/' << static_cast<std::uint32_t>(
						value.latest_source_sampled_windows)
					<< " pass_count=" << value.latest_gate_pass_count << ']';
			}
			output << '\n';
		}
		for (std::uint32_t eye{}; eye < 2; ++eye)
		{
			for (std::size_t index{};
				index < scene_batch_status.local_sample_counts[eye]; ++index)
			{
				const auto& sample = scene_batch_status.local_samples[eye][index];
				output << "    local_sample[" << eye << ':' << index << "]: executor="
					<< engine_stereo_scene_batch_probe::to_string(sample.executor);
				output << " consumer="
					<< engine_stereo_scene_batch_probe::to_string(sample.consumer);
				output << " call=" << sample.call_ordinal;
				output << " entry=" << sample.entry_index;
				output << " group=" << sample.group_index;
				output << " return=0x" << std::hex << sample.return_address;
				output << " source=0x" << sample.source_descriptor;
				output << " pointer=0x" << sample.source_pointer;
				output << " hash=0x" << sample.source_content_hash;
				output << " cursor=0x" << sample.cursor_pre
					<< "->0x" << sample.cursor_post
					<< "/0x" << sample.cursor_end << std::dec;
				output << " count=" << sample.source_count;
				output << " bytes=" << sample.source_byte_count;
				output << " sampled=" << sample.source_sampled_bytes << '/'
					<< static_cast<std::uint32_t>(sample.source_sampled_windows);
				output << " readable=" << yes_no(sample.source_content_readable) << '\n';
			}
			if (scene_batch_status.dropped_local_samples[eye] != 0)
			{
				output << "    local_samples_dropped[" << eye << "]="
					<< scene_batch_status.dropped_local_samples[eye] << '\n';
			}
		}
		for (std::uint32_t eye{}; eye < 2; ++eye)
		{
			for (std::size_t index{};
				index < scene_batch_status.local_gate_sample_counts[eye]; ++index)
			{
				const auto& sample = scene_batch_status.local_gate_samples[eye][index];
				output << "    local_gate[" << eye << ':' << index << "]: consumer="
					<< engine_stereo_scene_batch_probe::to_string(sample.consumer);
				output << " call=" << sample.call_ordinal;
				output << " accepted=" << yes_no(sample.accepted);
				output << " return=0x" << std::hex << sample.return_address;
				output << " context=0x" << sample.context;
				output << " key=0x" << sample.key;
				output << " words=0x" << sample.context_words[0]
					<< "/0x" << sample.context_words[1]
					<< "/0x" << sample.context_words[2]
					<< "/0x" << sample.context_words[3];
				output << " secondary=0x" << sample.secondary;
				output << " technique=0x" << sample.technique << std::dec;
				output << " pass_count=" << sample.pass_count;
				output << " context_readable=" << yes_no(sample.context_readable) << '\n';
				const auto& selection = sample.selection;
				output << "      material_selection: readable=" << yes_no(selection.readable)
					<< " pass_address_matches=" << yes_no(selection.pass_address_matches)
					<< " material=0x" << std::hex << selection.material
					<< " pass=0x" << selection.pass << std::dec
					<< " technique_type=" << selection.technique_type << " pass_index=" << selection.pass_index
					<< " material_name=" << (selection.material_name_readable ? selection.material_name.data() : "unknown")
					<< " technique_name=" << (selection.technique_name_readable ? selection.technique_name.data() : "unknown")
					<< " atlas(rows/cols/blend/array)=";
				for (const auto value : selection.atlas) output << static_cast<unsigned>(value) << '/';
				output << " shader_assets(vs/decl/hs/ds/ps)=";
				for (std::size_t shader{}; shader < 5; ++shader)
				{
					std::uintptr_t asset{};
					std::memcpy(&asset, selection.pass_bytes.data() + shader * sizeof(asset), sizeof(asset));
					output << "0x" << std::hex << asset << '/' << std::dec;
				}
				output << " arg_counts(prim/obj/stable)=";
				for (std::size_t arg = 0x29; arg < 0x2C; ++arg)
					output << static_cast<unsigned>(selection.pass_bytes[arg]) << '/';
				std::uintptr_t args{};
				std::memcpy(&args, selection.pass_bytes.data() + 0x40, sizeof(args));
				output << " args=0x" << std::hex << args << std::dec << '\n';
				output << "      state_bits: readable=" << yes_no(selection.state_bits_readable)
					<< " index=" << selection.state_bits_index << " words=";
				for (std::size_t offset{}; offset < selection.state_bits.size(); offset += 4)
				{
					std::uint32_t word{};
					std::memcpy(&word, selection.state_bits.data() + offset, sizeof(word));
					output << "0x" << std::hex << word << '/' << std::dec;
				}
				output << '\n';
			}
			if (scene_batch_status.dropped_local_gate_samples[eye] != 0)
			{
				output << "    local_gates_dropped[" << eye << "]="
					<< scene_batch_status.dropped_local_gate_samples[eye] << '\n';
			}
		}
		for (std::size_t index{}; index < scene_batch_status.sample_count; ++index)
		{
			const auto& sample = scene_batch_status.samples[index];
			output << "    mismatch[" << index << "]: kind="
				<< engine_stereo_scene_batch_probe::to_string(sample.kind);
			output << " boundary=" << sample.boundary_ordinal;
			output << " descriptor=" << sample.descriptor_index;
			output << " stream=" << sample.stream_index;
			output << " address=0x" << std::hex << sample.addresses[0]
				<< "/0x" << sample.addresses[1];
			output << " value=0x" << sample.values[0] << "/0x"
				<< sample.values[1] << std::dec;
			output << " bytes=" << sample.byte_counts[0] << '/'
				<< sample.byte_counts[1] << '\n';
		}
		if (scene_batch_status.dropped_samples != 0)
		{
			output << "    mismatch_samples_dropped="
				<< scene_batch_status.dropped_samples << '\n';
		}
		const auto& upload = owner_pass_status.dynamic_upload;
		output << "  backend_dynamic_upload: policy=once_after_both_scene_consumers"
			<< " pairs=" << upload.pairs << " complete=" << upload.complete
			<< " failures=" << upload.failures << " deferred=" << upload.deferred
			<< " advances=" << upload.advances << " recoveries=" << upload.recoveries
			<< " last_pair=" << upload.last_pair << " data=0x" << std::hex
			<< upload.last.data << std::dec << " thread=" << upload.last.thread
			<< " boundaries=" << upload.last.left_calls << '/' << upload.last.right_calls
			<< " phase=" << static_cast<unsigned>(upload.last.state)
			<< " error=" << engine_stereo_dynamic_upload::to_string(upload.last.error)
			<< " last_failure_pair=" << upload.last_failure_pair
			<< " last_failure=" << engine_stereo_dynamic_upload::to_string(
				upload.last_failure.error) << '\n';
	}
}
