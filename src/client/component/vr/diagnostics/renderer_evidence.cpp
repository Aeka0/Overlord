#include <std_include.hpp>
#include "renderer_evidence.hpp"
#include "../engine_stereo_renderer.hpp"
#include "../engine_backend_probe.hpp"
#include "../engine_stereo_binding.hpp"
#include "../engine_stereo_backend_view.hpp"
#include "../engine_stereo_backend_target.hpp"
#include "../engine_stereo_output_merger.hpp"
#include "../engine_stereo_draw_indexed.hpp"
#include "../engine_stereo_execution.hpp"
#include "component/console.hpp"
#include <utils/cryptography.hpp>
#include <utils/io.hpp>
#include <cstring>
#include <memory>
#include <sstream>

namespace vr::diagnostics::renderer_evidence
{
	namespace evidence = renderer_evidence;
	using engine_stereo_renderer::accepts_complete_execution_evidence;
	namespace
	{
		struct persistence_store
		{
			std::atomic_bool baseline_registry_summary_written{};
			std::atomic_bool before_registry_summary_written{};
			std::atomic_bool after_registry_summary_written{};
			std::atomic_bool backend_target_route_manifest_written{};
			std::atomic_bool backend_target_frame_manifest_written{};
			std::atomic_bool backend_output_merger_manifest_written{};
			std::atomic_bool backend_draw_indexed_manifest_written{};
			// Versions identify exact persisted bytes, including device/owner identity.
			std::atomic_uint64_t backend_execution_manifest_signature{};
			std::atomic_uint64_t artifact_manifest_signature{};
			std::atomic_bool artifact_completion_announced{};
			std::atomic_bool backend_execution_completion_announced{};
			std::atomic_uint64_t ownership_boundary_manifest_signature{
			    (std::numeric_limits<std::uint64_t>::max)()};
			std::atomic_bool retired_dual_record_manifest_written{};
			std::atomic_bool retired_owner_stereo_manifest_written{};
		};
		persistence_store persistence;
		constexpr auto evidence_directory = "minidumps/vr-native-stereo-evidence";
		template <std::size_t Size>
		[[nodiscard]] bool artifact_ready(const one_shot_artifact<Size>& artifact) noexcept
		{
			return artifact.state.load(std::memory_order_acquire) == 2;
		}

		template <std::size_t Size>
		bool write_artifact(one_shot_artifact<Size>& artifact, const char* const filename) noexcept
		{
			if (!artifact_ready(artifact))
				return false;
			if (artifact.written.load(std::memory_order_acquire))
				return true;
			const std::string bytes(reinterpret_cast<const char*>(artifact.bytes.data()), Size);
			std::ostringstream path;
			path << evidence_directory << '/' << filename;
			const auto written = utils::io::write_file_atomic(path.str(), bytes);
			if (written)
				artifact.written.store(true, std::memory_order_release);
			return written;
		}

		template <std::size_t Size>
		void append_artifact_manifest(std::ostringstream& output,
		                              const char* const name,
		                              const char* const filename,
		                              const one_shot_artifact<Size>& artifact)
		{
			const auto state = artifact.state.load(std::memory_order_acquire);
			const auto ready = state == 2;
			output << "artifact=" << name
			       << " state=" << (ready ? "ready" : (state == 1 ? "capturing" : "empty"))
			       << " written=" << (artifact.written.load(std::memory_order_acquire) ? "yes" : "no")
			       << " size=" << Size << " file=" << evidence_directory << '/' << filename;
			if (ready)
			{
				const std::string bytes(reinterpret_cast<const char*>(artifact.bytes.data()), Size);
				output << " sha256=" << utils::cryptography::sha256::compute(bytes, true);
				for (std::size_t index{}; index < artifact.metadata.size(); ++index)
				{
					output << " m" << index << "=0x" << std::hex << artifact.metadata[index] << std::dec;
				}
			}
			output << "\r\n";
		}

		template <std::size_t Size>
		void append_delta_manifest(std::ostringstream& output,
		                           const char* const name,
		                           const one_shot_artifact<Size>& before,
		                           const one_shot_artifact<Size>& after)
		{
			if (!artifact_ready(before) || !artifact_ready(after))
				return;
			std::size_t changed{};
			std::size_t first = Size;
			std::size_t last{};
			for (std::size_t index{}; index < Size; ++index)
			{
				if (before.bytes[index] == after.bytes[index])
					continue;
				if (changed++ == 0)
					first = index;
				last = index;
			}
			output << "delta=" << name << " changed_bytes=" << changed;
			if (changed != 0)
			{
				output << " first=0x" << std::hex << first << " last=0x" << last << std::dec;
			}
			output << "\r\n";
		}

		std::string format_target_registry_artifact(
		    const char* const label,
		    const one_shot_artifact<native_render_contract::target_registry_size>& artifact)
		{
			if (!artifact_ready(artifact))
				return {};
			constexpr auto qword_0_offset = std::size_t{0x00};
			constexpr auto qword_1_offset = std::size_t{0x08};
			constexpr auto qword_2_offset = std::size_t{0x10};
			constexpr auto qword_3_offset = std::size_t{0x18};
			constexpr auto width_offset = std::size_t{0x30};
			constexpr auto height_offset = std::size_t{0x32};
			constexpr auto related_offset = std::size_t{0x38};
			std::ostringstream entries;
			std::uint32_t pointer_occupied{};
			std::uint32_t dimensioned{};
			for (std::uint32_t id{}; id < native_render_contract::target_registry_capacity; ++id)
			{
				const auto* const entry =
				    artifact.bytes.data() +
				    static_cast<std::size_t>(id) * native_render_contract::target_registry_stride;
				std::uintptr_t qword_0{};
				std::uintptr_t qword_1{};
				std::uintptr_t qword_2{};
				std::uintptr_t qword_3{};
				std::uint16_t width{};
				std::uint16_t height{};
				std::uint32_t related{};
				std::memcpy(&qword_0, entry + qword_0_offset, sizeof(qword_0));
				std::memcpy(&qword_1, entry + qword_1_offset, sizeof(qword_1));
				std::memcpy(&qword_2, entry + qword_2_offset, sizeof(qword_2));
				std::memcpy(&qword_3, entry + qword_3_offset, sizeof(qword_3));
				std::memcpy(&width, entry + width_offset, sizeof(width));
				std::memcpy(&height, entry + height_offset, sizeof(height));
				std::memcpy(&related, entry + related_offset, sizeof(related));
				if (qword_0 != 0 || qword_1 != 0 || qword_2 != 0 || qword_3 != 0)
				{
					++pointer_occupied;
				}
				if (width != 0 || height != 0)
					++dimensioned;
				entries << "id=" << id << " qword0=0x" << std::hex << qword_0 << " qword1=0x" << qword_1
				        << " qword2=0x" << qword_2 << " qword3=0x" << qword_3 << std::dec << " size=" << width
				        << 'x' << height << " related=" << related << "\r\n";
			}
			std::ostringstream output;
			output
			    << "H2 target registry CPU snapshot\r\n"
			    << "label=" << label << "\r\n"
			    << "base=0x" << std::hex << native_render_contract::target_registry_base << std::dec
			    << " stride=" << native_render_contract::target_registry_stride
			    << " capacity=" << native_render_contract::target_registry_capacity
			    << " pointer_occupied=" << pointer_occupied << " dimensioned=" << dimensioned << "\r\n"
			    << "proven_binding_semantics=qword1_dsv; qword0_or_qword2_single_rtv; qword3_not_consumed_by_0x140781CA0\r\n"
			    << "classification=observation_only; zero fields do not prove a slot is free\r\n"
			    << entries.str();
			return output.str();
		}

		bool write_evidence_text_once(std::atomic_bool& written,
		                              const char* const filename,
		                              const std::string& value) noexcept
		{
			if (written.load(std::memory_order_acquire))
				return true;
			if (value.empty())
				return false;
			const auto success =
			    utils::io::write_file_atomic(std::string{evidence_directory} + '/' + filename, value);
			if (success)
				written.store(true, std::memory_order_release);
			return success;
		}

		bool write_evidence_text_versioned(std::atomic_uint64_t& written_signature,
		                                   const char* const filename,
		                                   const std::string& value,
		                                   const std::uint64_t signature) noexcept
		{
			if (value.empty() || signature == 0)
				return false;
			if (written_signature.load(std::memory_order_acquire) == signature)
				return true;
			const auto success =
			    utils::io::write_file_atomic(std::string{evidence_directory} + '/' + filename, value);
			if (success)
				written_signature.store(signature, std::memory_order_release);
			return success;
		}

		std::string format_backend_target_route(const engine_stereo_backend_target::report& report,
		                                        const engine_stereo_backend_target::status& status)
		{
			std::ostringstream output;
			output << "H2 backend target-route evidence\r\n"
			       << "state=" << engine_stereo_backend_target::to_string(status.state) << "\r\n"
			       << "contract=one_shot_cpu_observation_only\r\n"
			       << "registry_writes=0\r\nd3d_calls=0\r\nopenvr_calls=0\r\n"
			       << "publication_sequence=" << report.publication_sequence << " record=0x" << std::hex
			       << report.record << std::dec << "\r\n"
			       << "select_calls=" << status.select_calls
			       << " dispatch_select_calls=" << status.dispatch_select_calls
			       << " applied_transitions=" << status.applied_transitions
			       << " retained_targets=" << status.retained_targets
			       << " unique_targets=" << status.unique_targets
			       << " invalid_observations=" << status.invalid_observations
			       << " application_mismatches=" << status.application_mismatches
			       << " entry_mutations=" << status.entry_mutations
			       << " route_overflows=" << status.route_overflows << "\r\n";
			for (std::size_t index{}; index < report.event_count; ++index)
			{
				const auto& event = report.events[index];
				output << "event=" << index
				       << " phase=" << engine_stereo_backend_target::to_string(event.phase) << " caller=0x"
				       << std::hex << event.caller << " caller_rva=0x"
				       << (event.caller >= 0x140000000 ? event.caller - 0x140000000 : event.caller)
				       << " context=0x" << event.context << std::dec << " backend_state=0x"
				       << event.backend_state << std::dec << " target=" << event.target_id
				       << " current_before=" << event.current_before
				       << " current_after=" << event.current_after
				       << " entry_stable=" << (event.entry_stable ? "yes" : "no") << "\r\n";
			}
			for (std::uint32_t id{}; id < report.targets.size(); ++id)
			{
				const auto& target = report.targets[id];
				if (!target.seen)
					continue;
				std::array<std::uintptr_t, 4> pointers{};
				std::uint16_t width{};
				std::uint16_t height{};
				std::uint32_t related{};
				std::memcpy(pointers.data(), target.bytes.data(), pointers.size() * sizeof(std::uintptr_t));
				std::memcpy(&width, target.bytes.data() + 0x30, sizeof(width));
				std::memcpy(&height, target.bytes.data() + 0x32, sizeof(height));
				std::memcpy(&related, target.bytes.data() + 0x38, sizeof(related));
				output << "target=" << id << " qword0=0x" << std::hex << pointers[0] << " qword1=0x"
				       << pointers[1] << " qword2=0x" << pointers[2] << " qword3=0x" << pointers[3]
				       << std::dec << " size=" << width << 'x' << height << " related=" << related << "\r\n";
			}
			return output.str();
		}

		std::string format_backend_target_frame(const engine_stereo_backend_target::frame_report& report,
		                                        const engine_stereo_backend_target::frame_status& status)
		{
			std::ostringstream output;
			output
			    << "H2 backend target frame-route evidence\r\n"
			    << "state=" << engine_stereo_backend_target::to_string(status.state) << "\r\n"
			    << "contract=one_shot_claim_to_following_present_pre_cpu_observation\r\n"
			    << "registry_writes=0\r\nd3d_calls=0\r\nopenvr_calls=0\r\n"
			    << "registry_semantics=qword1_dsv; qword0_or_qword2_single_rtv; qword3_not_consumed_by_0x140781CA0\r\n"
			    << "publication_sequence=" << report.start_publication_sequence << " record=0x" << std::hex
			    << report.start_record << std::dec << " present_post=" << report.start_present_post_frame
			    << " present_post_thread=" << report.start_present_post_thread_id
			    << " present_pre=" << report.end_present_pre_frame
			    << " generation=" << report.device_generation
			    << " boundary_thread=" << report.boundary_thread_id << "\r\n"
			    << "select_calls=" << status.select_calls << " view_copy_events=" << status.view_copy_events
			    << " events=" << report.event_count
			    << " report_view_copy_events=" << report.view_copy_event_count
			    << " unique_targets=" << report.unique_target_count
			    << " invalid_observations=" << status.invalid_observations
			    << " application_mismatches=" << status.application_mismatches
			    << " entry_mutations=" << status.entry_mutations
			    << " route_overflows=" << status.route_overflows
			    << " generation_mismatches=" << status.device_generation_mismatches
			    << " max_concurrent_writers=" << status.maximum_active_writers << "\r\n";
			for (std::size_t index{}; index < report.event_count; ++index)
			{
				const auto& event = report.events[index];
				output << "event=" << index << " sequence=" << event.sequence
				       << " kind=" << engine_stereo_backend_target::to_string(event.kind)
				       << " qpc=" << event.timestamp_qpc
				       << " present_post=" << event.latest_present_post_frame << " thread=" << event.thread_id
				       << " phase=" << engine_stereo_backend_target::to_string(event.phase) << " caller=0x"
				       << std::hex << event.caller << " caller_rva=0x"
				       << (event.caller >= 0x140000000 ? event.caller - 0x140000000 : event.caller)
				       << " record=0x" << event.record << std::dec << " backend_id=" << event.backend_id
				       << " publication=" << event.publication_sequence
				       << " record_type=" << event.record_type
				       << " backend_active=" << (event.backend_active ? "yes" : "no")
				       << " binding_active=" << (event.binding_active ? "yes" : "no");
				if (event.kind == engine_stereo_backend_target::frame_event_kind::view_copy)
				{
					output << " copy_ordinal=" << event.view_copy_ordinal
					       << " substitution_ordinal=" << event.view_substitution_ordinal
					       << " selected_eye=" << event.selected_eye
					       << " source_matched=" << (event.view_source_matched ? "yes" : "no") << "\r\n";
					continue;
				}
				std::array<std::uintptr_t, 4> qwords{};
				std::uint16_t width{};
				std::uint16_t height{};
				std::uint32_t related{};
				if (event.entry_valid)
				{
					std::memcpy(
					    qwords.data(), event.target_entry.data(), qwords.size() * sizeof(std::uintptr_t));
					std::memcpy(&width, event.target_entry.data() + 0x30, sizeof(width));
					std::memcpy(&height, event.target_entry.data() + 0x32, sizeof(height));
					std::memcpy(&related, event.target_entry.data() + 0x38, sizeof(related));
				}
				output << " context=0x" << std::hex << event.context << " backend_state=0x"
				       << event.backend_state << std::dec << " target=" << event.target_id
				       << " current_before=" << event.current_before
				       << " current_after=" << event.current_after
				       << " entry_valid=" << (event.entry_valid ? "yes" : "no")
				       << " entry_stable=" << (event.entry_stable ? "yes" : "no")
				       << " application_matches=" << (event.application_matches ? "yes" : "no")
				       << " qword0=0x" << std::hex << qwords[0] << " qword1=0x" << qwords[1] << " qword2=0x"
				       << qwords[2] << " qword3=0x" << qwords[3] << std::dec << " size=" << width << 'x'
				       << height << " related=" << related << "\r\n";
			}
			for (std::uint32_t id{}; id < report.targets.size(); ++id)
			{
				const auto& target = report.targets[id];
				if (!target.seen)
					continue;
				std::array<std::uintptr_t, 4> qwords{};
				std::uint16_t width{};
				std::uint16_t height{};
				std::uint32_t related{};
				std::memcpy(qwords.data(), target.bytes.data(), qwords.size() * sizeof(std::uintptr_t));
				std::memcpy(&width, target.bytes.data() + 0x30, sizeof(width));
				std::memcpy(&height, target.bytes.data() + 0x32, sizeof(height));
				std::memcpy(&related, target.bytes.data() + 0x38, sizeof(related));
				output << "target=" << id << " qword0=0x" << std::hex << qwords[0] << " qword1=0x"
				       << qwords[1] << " qword2=0x" << qwords[2] << " qword3=0x" << qwords[3] << std::dec
				       << " size=" << width << 'x' << height << " related=" << related << "\r\n";
			}
			return output.str();
		}

		void append_output_view(std::ostringstream& output,
		                        const char* const label,
		                        const std::size_t index,
		                        const engine_stereo_output_merger::view_observation& view)
		{
			std::string prefix{label};
			if (index != (std::numeric_limits<std::size_t>::max)())
			{
				prefix += std::to_string(index);
			}
			output << ' ' << prefix << "_view=0x" << std::hex << view.view << ' ' << prefix << "_resource=0x"
			       << view.resource << std::dec << ' ' << prefix << "_valid=" << (view.valid ? "yes" : "no")
			       << ' ' << prefix << "_view_format=" << static_cast<std::uint32_t>(view.view_format) << ' '
			       << prefix << "_view_dimension=" << view.view_dimension << ' ' << prefix
			       << "_resource_dimension=" << static_cast<std::uint32_t>(view.resource_dimension) << ' '
			       << prefix << "_size=" << view.width << 'x' << view.height << ' ' << prefix
			       << "_resource_format=" << static_cast<std::uint32_t>(view.resource_format) << ' ' << prefix
			       << "_mips=" << view.mip_levels << ' ' << prefix << "_array=" << view.array_size << ' '
			       << prefix << "_samples=" << view.sample_count << ':' << view.sample_quality << ' '
			       << prefix << "_usage=" << static_cast<std::uint32_t>(view.usage) << ' ' << prefix
			       << "_bind=0x" << std::hex << view.bind_flags << ' ' << prefix << "_cpu=0x"
			       << view.cpu_access_flags << ' ' << prefix << "_misc=0x" << view.misc_flags << std::dec;
		}

		std::string format_backend_output_merger(const engine_stereo_output_merger::report& report,
		                                         const engine_stereo_output_merger::status& status)
		{
			std::ostringstream output;
			output << "H2 backend D3D11 output-merger evidence\r\n"
			       << "state=" << engine_stereo_output_merger::to_string(status.state) << "\r\n"
			       << "contract=one_shot_exact_backend_transaction_read_only_om_observation\r\n"
			       << "additional_gpu_commands=0\r\ntarget_replacements=0\r\n"
			       << "command_replays=0\r\nopenvr_calls=0\r\n"
			       << "metadata_queries=" << status.metadata_queries << "\r\n"
			       << "publication_sequence=" << report.publication_sequence << " record=0x" << std::hex
			       << report.record << " expected_context=0x" << report.expected_context << std::dec
			       << " generation=" << report.device_generation
			       << " transaction_thread=" << report.transaction_thread_id << "\r\n"
			       << "bind_calls=" << status.bind_calls
			       << " nonnull_bind_calls=" << status.nonnull_bind_calls << " events=" << report.event_count
			       << " context_mismatches=" << status.context_mismatches
			       << " thread_mismatches=" << status.thread_mismatches
			       << " query_failures=" << status.query_failures << " overflows=" << status.overflows
			       << "\r\n";
			for (std::size_t event_index{}; event_index < report.event_count; ++event_index)
			{
				const auto& event = report.events[event_index];
				output << "event=" << event_index << " sequence=" << event.sequence
				       << " qpc=" << event.timestamp_qpc << " thread=" << event.thread_id
				       << " phase=" << engine_stereo_output_merger::to_string(event.execution_phase)
				       << " caller=0x" << std::hex << event.caller << " caller_rva=0x"
				       << (event.caller >= 0x140000000 ? event.caller - 0x140000000 : event.caller)
				       << " context=0x" << event.context << std::dec
				       << " expected_context=" << (event.expected_context ? "yes" : "no")
				       << " target=" << event.latest_target_id << " copy_ordinal=" << event.view_copy_ordinal
				       << " substitution_ordinal=" << event.view_substitution_ordinal
				       << " selected_eye=" << event.selected_eye
				       << " render_target_count=" << event.render_target_count;
				const auto target_count = (std::min)(static_cast<std::size_t>(event.render_target_count),
				                                     event.render_targets.size());
				for (std::size_t target_index{}; target_index < target_count; ++target_index)
				{
					append_output_view(output, "rtv", target_index, event.render_targets[target_index]);
				}
				append_output_view(
				    output, "dsv", (std::numeric_limits<std::size_t>::max)(), event.depth_stencil);
				output << "\r\n";
			}
			return output.str();
		}

		std::string format_backend_draw_indexed(const engine_stereo_draw_indexed::report& report,
		                                        const engine_stereo_draw_indexed::status& status)
		{
			std::ostringstream output;
			output << "H2 backend DrawIndexed replayability evidence\r\n"
			       << "state=" << engine_stereo_draw_indexed::to_string(status.state) << "\r\n"
			       << "contract=one_shot_exact_dispatch_read_only_draw_indexed_observation\r\n"
			       << "additional_gpu_commands=0\r\ntarget_replacements=0\r\n"
			       << "command_replays=0\r\nopenvr_calls=0\r\n"
			       << "publication_sequence=" << report.publication_sequence << " record=0x" << std::hex
			       << report.record << " expected_context=0x" << report.expected_context << std::dec
			       << " generation=" << report.device_generation
			       << " transaction_thread=" << report.transaction_thread_id << "\r\n"
			       << "command_before_address=0x" << std::hex << report.before.address
			       << " command_before_hash=0x" << report.before.hash << std::dec
			       << " command_before_bytes=" << report.before.byte_count
			       << " command_before_records=" << report.before.record_count
			       << " command_before_valid=" << (report.before.valid ? "yes" : "no") << "\r\n"
			       << "command_after_address=0x" << std::hex << report.after.address
			       << " command_after_hash=0x" << report.after.hash << std::dec
			       << " command_after_bytes=" << report.after.byte_count
			       << " command_after_records=" << report.after.record_count
			       << " command_after_valid=" << (report.after.valid ? "yes" : "no") << "\r\n"
			       << "command_stream_immutable="
			       << (report.before.valid && report.after.valid &&
			                   report.before.address == report.after.address &&
			                   report.before.byte_count == report.after.byte_count &&
			                   report.before.record_count == report.after.record_count &&
			                   report.before.hash == report.after.hash
			               ? "yes"
			               : "no")
			       << "\r\n"
			       << "draw_calls=" << status.draw_calls << " events=" << report.event_count
			       << " draw_call_hash=0x" << std::hex << report.draw_call_hash << std::dec
			       << " context_mismatches=" << status.context_mismatches
			       << " thread_mismatches=" << status.thread_mismatches
			       << " invalid_arguments=" << status.invalid_arguments << " overflows=" << status.overflows
			       << "\r\n";
			for (std::size_t event_index{}; event_index < report.event_count; ++event_index)
			{
				const auto& event = report.events[event_index];
				output << "event=" << event_index << " sequence=" << event.sequence
				       << " qpc=" << event.timestamp_qpc << " thread=" << event.thread_id << " caller=0x"
				       << std::hex << event.caller << " caller_rva=0x"
				       << (event.caller >= 0x140000000 ? event.caller - 0x140000000 : event.caller)
				       << " context=0x" << event.context << std::dec
				       << " expected_context=" << (event.expected_context ? "yes" : "no")
				       << " arguments_valid=" << (event.arguments_valid ? "yes" : "no")
				       << " index_count=" << event.index_count
				       << " start_index=" << event.start_index_location
				       << " base_vertex=" << event.base_vertex_location << "\r\n";
			}
			output << "boundary_draw_calls=" << report.boundary_draw_calls
			       << " boundary_groups=" << report.boundary_group_count
			       << " boundary_group_overflows=" << report.boundary_group_overflows << "\r\n";
			for (std::size_t group_index{}; group_index < report.boundary_group_count; ++group_index)
			{
				const auto& group = report.boundary_groups[group_index];
				output << "boundary_group=" << group_index
				       << " phase=" << engine_stereo_draw_indexed::to_string(group.execution_phase)
				       << " binding_sequence=" << group.binding_sequence << " target=" << group.target_id
				       << " render_target_count=" << group.render_target_count << " rtv0=0x" << std::hex
				       << group.render_target << " dsv=0x" << group.depth_stencil << std::dec
				       << " draws=" << group.draw_calls << " indices=" << group.index_count << " hash=0x"
				       << std::hex << group.draw_call_hash << " first_caller_rva=0x"
				       << (group.first_caller >= 0x140000000 ? group.first_caller - 0x140000000
				                                             : group.first_caller)
				       << " last_caller_rva=0x"
				       << (group.last_caller >= 0x140000000 ? group.last_caller - 0x140000000
				                                            : group.last_caller)
				       << std::dec << " first_start_index=" << group.first_start_index
				       << " last_start_index=" << group.last_start_index
				       << " first_base_vertex=" << group.first_base_vertex
				       << " last_base_vertex=" << group.last_base_vertex << "\r\n";
			}
			return output.str();
		}

		const char* execution_scope_name(const std::uint8_t flags) noexcept
		{
			if ((flags & engine_stereo_execution::classifier_scope_flag) != 0)
			{
				return "classifier";
			}
			if ((flags & engine_stereo_execution::frame_scope_flag) != 0)
			{
				return "frame";
			}
			return "none";
		}

		std::string format_backend_execution(const engine_stereo_execution::report& report,
		                                     const engine_stereo_execution::status& status)
		{
			std::ostringstream output;
			output << "H2 D3D11 execution census\r\n"
			       << "state=" << engine_stereo_execution::to_string(status.state) << "\r\n"
			       << "error=" << engine_stereo_execution::to_string(status.error) << "\r\n"
			       << "contract=exact_type4_classifier_plus_next_present_to_present_frame_read_only\r\n"
			       << "scene_owner_contract=outer_current_record_0x7a7da0_to_0x7a8306_cpu_lifetime\r\n"
			       << "additional_gpu_commands=0\r\ntarget_replacements=0\r\n"
			       << "command_replays=0\r\nopenvr_calls=0\r\n"
			       << "expected_context=0x" << std::hex << report.expected_context << std::dec
			       << " generation=" << report.device_generation
			       << " hooks_installed=" << (status.hooks_installed ? "yes" : "no")
			       << " installed_hook_count=" << status.installed_hook_count
			       << " targets_distinct=" << (status.targets_distinct ? "yes" : "no")
			       << " permanent_install_failure=" << (status.installation_permanently_failed ? "yes" : "no")
			       << " draw_indexed_forwarding_external="
			       << (status.draw_indexed_forwarding_external ? "yes" : "no")
			       << " draw_indexed_forwarded_calls=" << status.draw_indexed_forwarded_calls
			       << " hook_failures=" << status.hook_failures << " om_target=0x" << std::hex
			       << status.output_merger_target << " om_uav_target=0x"
			       << status.output_merger_unordered_access_target << " clear_state_target=0x"
			       << status.clear_state_target << std::dec << "\r\n"
			       << "classifier_record=0x" << std::hex << report.classifier_record << std::dec
			       << " classifier_type=" << report.classifier_record_type
			       << " classifier_thread=" << report.classifier_thread_id
			       << " classifier_qpc=" << report.classifier_begin_qpc << "->" << report.classifier_end_qpc
			       << "\r\n"
			       << "frame_present=" << report.frame_start_present_post << "->"
			       << report.frame_end_present_pre << "->" << report.frame_end_present_post << " result=0x"
			       << std::hex << static_cast<std::uint32_t>(report.frame_present_result) << std::dec
			       << " frame_threads=" << report.frame_start_thread_id << "->" << report.frame_end_thread_id
			       << "\r\n"
			       << "event_reservations=" << status.event_reservations << " events=" << report.event_count
			       << " classifier_events=" << report.classifier_event_count
			       << " frame_events=" << report.frame_event_count
			       << " expected_context_frame_events=" << report.expected_context_frame_events
			       << " backend_scoped_events=" << report.backend_scoped_events
			       << " backend_scoped_frame_events=" << report.backend_scoped_frame_events
			       << " backend_unscoped_frame_events=" << report.backend_unscoped_frame_events
			       << " backend_thread_mismatches=" << report.backend_thread_mismatches
			       << " distinct_backend_records=" << report.distinct_backend_records
			       << " scene_owner_scoped_events=" << report.scene_owner_scoped_events
			       << " scene_owner_scoped_frame_events=" << report.scene_owner_scoped_frame_events
			       << " scene_owner_unscoped_frame_events=" << report.scene_owner_unscoped_frame_events
			       << " scene_owner_thread_mismatches=" << report.scene_owner_thread_mismatches
			       << " distinct_scene_owner_records=" << report.distinct_scene_owner_records
			       << " call_stack_samples=" << report.call_stack_samples
			       << " call_stack_capture_failures=" << report.call_stack_capture_failures
			       << " call_stack_key_overflows=" << report.call_stack_key_overflows
			       << " admission_collisions=" << report.admission_collisions
			       << " overflows=" << report.overflow_count
			       << " foreign_context_events=" << report.foreign_context_events
			       << " classifier_thread_mismatches=" << report.classifier_thread_mismatches
			       << " distinct_contexts=" << report.distinct_contexts
			       << " distinct_threads=" << report.distinct_threads
			       << " execute_command_lists=" << report.opaque_execute_command_lists
			       << " known_conversion_recordings=" << report.known_conversion_recordings
			       << " known_conversion_executions=" << report.known_conversion_executions
			       << " known_conversion_replays=" << report.known_conversion_replays
			       << " deferred_execution_opaque=" << (report.deferred_execution_opaque ? "yes" : "no")
			       << " identity_truncated=" << (report.identity_truncated ? "yes" : "no")
			       << " max_concurrent_writers=" << status.maximum_active_writers << "\r\n";
			for (std::size_t index{}; index < engine_stereo_execution::api_count; ++index)
			{
				const auto operation = static_cast<engine_stereo_execution::api>(index);
				output << "api=" << engine_stereo_execution::to_string(operation) << " hook_target=0x"
				       << std::hex << status.hook_targets[index] << std::dec
				       << " total=" << report.per_api[index]
				       << " classifier=" << report.classifier_per_api[index]
				       << " frame=" << report.frame_per_api[index] << "\r\n";
			}
			for (std::uint32_t event_index{}; event_index < report.event_count; ++event_index)
			{
				const auto& event = report.events[event_index];
				output << "event=" << event_index << " sequence=" << event.sequence
				       << " scope=" << execution_scope_name(event.scope_flags)
				       << " api=" << engine_stereo_execution::to_string(event.operation)
				       << " qpc=" << event.timestamp_qpc << " thread=" << event.thread_id << " caller=0x"
				       << std::hex << event.caller;
				constexpr auto h2_image_base = std::uintptr_t{0x140000000};
				constexpr auto h2_image_end = std::uintptr_t{0x151E6BC00};
				if (event.caller >= h2_image_base && event.caller < h2_image_end)
				{
					output << " caller_rva=0x" << (event.caller - h2_image_base);
				}
				else
				{
					output << " caller_rva=external";
				}
				output << " context=0x" << event.context << std::dec
				       << " expected_context=" << (event.expected_context ? "yes" : "no")
				       << " argument_count=" << static_cast<std::uint32_t>(event.argument_count);
				for (std::size_t argument_index{};
				     argument_index < event.argument_count && argument_index < event.arguments.size();
				     ++argument_index)
				{
					output << " arg" << argument_index << "=0x" << std::hex << event.arguments[argument_index]
					       << std::dec;
				}
				output << " binding_valid=" << (event.output_binding.valid ? "yes" : "no")
				       << " binding_sequence=" << event.output_binding.sequence
				       << " target=" << event.output_binding.target_id
				       << " rtv_count=" << event.output_binding.render_target_count << " rtv0=0x" << std::hex
				       << event.output_binding.render_target_0 << " dsv=0x"
				       << event.output_binding.depth_stencil << std::dec
				       << " backend_active=" << (event.backend ? "yes" : "no")
				       << " backend_id=" << event.backend.backend_id << " backend_record=0x" << std::hex
				       << event.backend.record << " backend_frontend=0x" << event.backend.frontend
				       << " backend_commands=0x" << event.backend.command_stream << std::dec
				       << " backend_record_index=" << event.backend.record_index
				       << " backend_record_type=" << event.backend.record_type
				       << " backend_target=" << event.backend.target_id
				       << " backend_owner_thread=" << event.backend.owner_thread_id
				       << " backend_thread_match=" << (event.backend_thread_match ? "yes" : "no")
				       << " backend_dispatch_entered=" << (event.backend.dispatch_entered ? "yes" : "no")
				       << " frontend_epoch=" << event.backend.frontend_epoch
				       << " frontend_transaction=" << event.backend.frontend_transaction_id
				       << " scene_owner_active=" << (event.scene_owner ? "yes" : "no")
				       << " scene_owner_id=" << event.scene_owner.observation_id << " scene_owner_record=0x"
				       << std::hex << event.scene_owner.record << " scene_owner_frontend=0x"
				       << event.scene_owner.frontend << " scene_owner_caller=0x" << event.scene_owner.caller
				       << std::dec << " scene_owner_record_index=" << event.scene_owner.record_index
				       << " scene_owner_record_type=" << event.scene_owner.record_type
				       << " scene_owner_targets=" << event.scene_owner.target_ids[0] << ','
				       << event.scene_owner.target_ids[1] << ',' << event.scene_owner.target_ids[2]
				       << " scene_owner_selector=" << event.scene_owner.target_selector
				       << " scene_owner_thread=" << event.scene_owner.owner_thread_id
				       << " scene_owner_thread_match=" << (event.scene_owner_thread_match ? "yes" : "no")
				       << " scene_owner_record_valid=" << (event.scene_owner.record_valid ? "yes" : "no")
				       << " stack_depth=" << static_cast<std::uint32_t>(event.call_stack_depth);
				for (std::size_t stack_index{};
				     stack_index < event.call_stack_depth && stack_index < event.call_stack.size();
				     ++stack_index)
				{
					output << " stack" << stack_index << "=0x" << std::hex << event.call_stack[stack_index]
					       << std::dec;
				}
				output << "\r\n";
			}
			return output.str();
		}

	}
	bool checkpoint() noexcept
	{
		try
		{
			const auto boundary_frame =
			    evidence::captures.ownership_boundary_capture_frame.load(std::memory_order_acquire);
			std::uint64_t boundary_ready_mask{};
			std::ostringstream boundary_manifest;
			boundary_manifest << "contract=cpu_observation_only\r\n"
			                  << "frontend_mutation=none\r\n"
			                  << "d3d_calls=0\r\nopenvr_calls=0\r\n"
			                  << "capture_frame=" << boundary_frame << "\r\n";
			for (std::size_t boundary_index{}; boundary_index < ownership_boundary_count; ++boundary_index)
			{
				for (std::size_t stage_index{}; stage_index < ownership_boundary_stage_count; ++stage_index)
				{
					const auto artifact_index = boundary_index * ownership_boundary_stage_count + stage_index;
					const auto& artifact = evidence::captures.ownership_boundary_artifacts[artifact_index];
					const auto ready = artifact.state.load(std::memory_order_acquire) == 2;
					if (ready)
						boundary_ready_mask |= std::uint64_t{1} << artifact_index;
					const auto boundary =
					    static_cast<engine_view_probe::ownership_boundary_kind>(boundary_index + 1);
					const auto stage = stage_index == 0 ? engine_view_probe::observation_stage::enter
					                                    : engine_view_probe::observation_stage::leave;
					boundary_manifest << "artifact=" << engine_view_probe::to_string(boundary) << '_'
					                  << engine_view_probe::to_string(stage)
					                  << " state=" << (ready ? "ready" : "empty");
					if (ready)
					{
						const auto& value = artifact.observation;
						boundary_manifest
						    << " frame=" << artifact.frontend_frame_id << " tick=" << artifact.capture_tick
						    << " thread=" << artifact.thread_id << " frontend=0x" << std::hex
						    << value.frontend << " backend_frontend=0x" << value.backend_frontend
						    << " record_arena=0x" << value.record_arena << std::dec
						    << " selector=" << value.selector << " slot_count=" << value.slot_count
						    << " current_record_index=" << value.current_record_index
						    << " record_count=" << value.record_count
						    << " global_record_count=" << value.global_record_count
						    << " owner_record_index=" << value.owner_record_index << " owner_view_hash=0x"
						    << std::hex << value.owner_view_hash << std::dec;
					}
					boundary_manifest << "\r\n";
				}
			}
			constexpr auto complete_boundary_mask =
			    (std::uint64_t{1} << (ownership_boundary_count * ownership_boundary_stage_count)) - 1;
			const auto boundary_complete = boundary_ready_mask == complete_boundary_mask;
			boundary_manifest << "state=" << (boundary_complete ? "complete" : "waiting_for_in_level_scene")
			                  << "\r\n";
			const auto boundary_signature = (boundary_frame << 8) | boundary_ready_mask;
			if (persistence.ownership_boundary_manifest_signature.load(std::memory_order_acquire) !=
			        boundary_signature &&
			    utils::io::write_file_atomic(std::string{evidence_directory} +
			                                     "/ownership-boundary-manifest.txt",
			                                 boundary_manifest.str()))
			{
				persistence.ownership_boundary_manifest_signature.store(boundary_signature,
				                                                        std::memory_order_release);
			}

			engine_stereo_renderer::capture_registry_baseline();

			bool files_complete = true;
			files_complete = write_artifact(evidence::captures.baseline_target_registry_artifact,
			                                "target-registry-baseline.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.descriptor_before_artifact,
			                                "scene-descriptor-before.bin") &&
			                 files_complete;
			files_complete =
			    write_artifact(evidence::captures.descriptor_after_artifact, "scene-descriptor-after.bin") &&
			    files_complete;
			files_complete = write_artifact(evidence::captures.slot_before_initializer_artifact,
			                                "view-slot-before-initializer.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.slot_after_initializer_artifact,
			                                "view-slot-after-initializer.bin") &&
			                 files_complete;
			files_complete =
			    write_artifact(evidence::captures.stereo_eye_slot_artifacts[0], "stereo-eye-left-slot.bin") &&
			    files_complete;
			files_complete = write_artifact(evidence::captures.stereo_eye_slot_artifacts[1],
			                                "stereo-eye-right-slot.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.backend_view_source_before_artifact,
			                                "backend-view-source-before.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.backend_view_source_after_artifact,
			                                "backend-view-source-after.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.backend_bound_eye_slot_artifacts[0],
			                                "backend-bound-left-slot.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.backend_bound_eye_slot_artifacts[1],
			                                "backend-bound-right-slot.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.slot_after_generator_artifact,
			                                "view-slot-after-generator.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.output_after_generator_artifact,
			                                "per-client-output-after-generator.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.record_before_target_prepare_artifact,
			                                "backend-record-before-target-prepare.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.record_after_target_prepare_artifact,
			                                "backend-record-after-target-prepare.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.registry_before_target_prepare_artifact,
			                                "target-registry-before-target-prepare.bin") &&
			                 files_complete;
			files_complete = write_artifact(evidence::captures.registry_after_target_prepare_artifact,
			                                "target-registry-after-target-prepare.bin") &&
			                 files_complete;

			const auto baseline_summary =
			    persistence.baseline_registry_summary_written.load(std::memory_order_acquire)
			        ? std::string{}
			        : format_target_registry_artifact("baseline",
			                                          evidence::captures.baseline_target_registry_artifact);
			const auto before_summary =
			    persistence.before_registry_summary_written.load(std::memory_order_acquire)
			        ? std::string{}
			        : format_target_registry_artifact(
			              "before_target_prepare",
			              evidence::captures.registry_before_target_prepare_artifact);
			const auto after_summary =
			    persistence.after_registry_summary_written.load(std::memory_order_acquire)
			        ? std::string{}
			        : format_target_registry_artifact(
			              "after_target_prepare", evidence::captures.registry_after_target_prepare_artifact);
			const auto baseline_summary_complete =
			    write_evidence_text_once(persistence.baseline_registry_summary_written,
			                             "target-registry-baseline.txt",
			                             baseline_summary);
			const auto before_summary_complete =
			    write_evidence_text_once(persistence.before_registry_summary_written,
			                             "target-registry-before-target-prepare.txt",
			                             before_summary);
			const auto after_summary_complete =
			    write_evidence_text_once(persistence.after_registry_summary_written,
			                             "target-registry-after-target-prepare.txt",
			                             after_summary);
			const auto summaries_complete =
			    baseline_summary_complete && before_summary_complete && after_summary_complete;

			const auto all_ready =
			    artifact_ready(evidence::captures.baseline_target_registry_artifact) &&
			    artifact_ready(evidence::captures.descriptor_before_artifact) &&
			    artifact_ready(evidence::captures.descriptor_after_artifact) &&
			    artifact_ready(evidence::captures.slot_before_initializer_artifact) &&
			    artifact_ready(evidence::captures.slot_after_initializer_artifact) &&
			    artifact_ready(evidence::captures.stereo_eye_slot_artifacts[0]) &&
			    artifact_ready(evidence::captures.stereo_eye_slot_artifacts[1]) &&
			    artifact_ready(evidence::captures.backend_view_source_before_artifact) &&
			    artifact_ready(evidence::captures.backend_view_source_after_artifact) &&
			    artifact_ready(evidence::captures.backend_bound_eye_slot_artifacts[0]) &&
			    artifact_ready(evidence::captures.backend_bound_eye_slot_artifacts[1]) &&
			    artifact_ready(evidence::captures.slot_after_generator_artifact) &&
			    artifact_ready(evidence::captures.output_after_generator_artifact) &&
			    artifact_ready(evidence::captures.record_before_target_prepare_artifact) &&
			    artifact_ready(evidence::captures.record_after_target_prepare_artifact) &&
			    artifact_ready(evidence::captures.registry_before_target_prepare_artifact) &&
			    artifact_ready(evidence::captures.registry_after_target_prepare_artifact);
			const auto binding = engine_stereo_binding::get_status();
			const auto binding_complete = binding.publications != 0 && binding.claims != 0 &&
			                              binding.releases != 0 && binding.publication_drops == 0 &&
			                              binding.mapping_misses == 0 && binding.invalid_publications == 0 &&
			                              binding.release_mismatches == 0 && binding.active_claims == 0;
			const auto backend_view = engine_stereo_backend_view::get_status();
			const auto backend_view_complete =
			    backend_view.state == engine_stereo_backend_view::gate_state::complete &&
			    backend_view.attempts == 1 && backend_view.completions == 1 && backend_view.failures == 0 &&
			    backend_view.copy_calls != 0 && backend_view.substitutions != 0 &&
			    backend_view.foreign_copy_bypasses == 0 && backend_view.destination_mismatches == 0 &&
			    backend_view.record_mutations == 0 && backend_view.dispatch_misses == 0;
			const auto backend_target = engine_stereo_backend_target::get_status();
			engine_stereo_backend_target::report backend_target_report{};
			const auto backend_target_report_ready =
			    engine_stereo_backend_target::read_report(backend_target_report);
			const auto backend_target_manifest =
			    backend_target_report_ready
			        ? format_backend_target_route(backend_target_report, backend_target)
			        : std::string{};
			const auto backend_target_manifest_complete =
			    write_evidence_text_once(persistence.backend_target_route_manifest_written,
			                             "backend-target-route-manifest.txt",
			                             backend_target_manifest);
			const auto backend_target_complete =
			    backend_target.state == engine_stereo_backend_target::gate_state::complete &&
			    backend_target.attempts == 1 && backend_target.completions == 1 &&
			    backend_target.failures == 0 && backend_target.select_calls != 0 &&
			    backend_target.applied_transitions != 0 && backend_target.unique_targets != 0 &&
			    backend_target.invalid_observations == 0 && backend_target.application_mismatches == 0 &&
			    backend_target.entry_mutations == 0 && backend_target.route_overflows == 0 &&
			    backend_target_manifest_complete;
			const auto backend_target_frame = engine_stereo_backend_target::get_frame_status();
			std::unique_ptr<engine_stereo_backend_target::frame_report> backend_target_frame_report;
			bool backend_target_frame_report_ready{};
			if (backend_target_frame.state == engine_stereo_backend_target::gate_state::complete ||
			    backend_target_frame.state == engine_stereo_backend_target::gate_state::failed)
			{
				backend_target_frame_report = std::make_unique<engine_stereo_backend_target::frame_report>();
				backend_target_frame_report_ready =
				    engine_stereo_backend_target::read_frame_report(*backend_target_frame_report);
			}
			const auto backend_target_frame_manifest =
			    backend_target_frame_report_ready
			        ? format_backend_target_frame(*backend_target_frame_report, backend_target_frame)
			        : std::string{};
			const auto backend_target_frame_manifest_complete =
			    write_evidence_text_once(persistence.backend_target_frame_manifest_written,
			                             "backend-target-frame-manifest.txt",
			                             backend_target_frame_manifest);
			const auto backend_target_frame_report_consistent =
			    backend_target_frame_report_ready &&
			    backend_target_frame_report->start_publication_sequence ==
			        backend_target_frame.start_publication_sequence &&
			    backend_target_frame_report->start_record == backend_target_frame.start_record &&
			    backend_target_frame_report->start_present_post_frame ==
			        backend_target_frame.start_present_post_frame &&
			    backend_target_frame_report->start_present_post_thread_id ==
			        backend_target_frame.start_present_post_thread_id &&
			    backend_target_frame_report->end_present_pre_frame ==
			        backend_target_frame.end_present_pre_frame &&
			    backend_target_frame_report->device_generation == backend_target_frame.device_generation &&
			    backend_target_frame_report->boundary_thread_id == backend_target_frame.boundary_thread_id &&
			    backend_target_frame_report->event_count ==
			        backend_target_frame.select_calls + backend_target_frame.view_copy_events &&
			    backend_target_frame_report->view_copy_event_count == backend_target_frame.view_copy_events &&
			    backend_target_frame_report->unique_target_count == backend_target_frame.unique_targets;
			const auto backend_target_frame_complete =
			    backend_target_frame.state == engine_stereo_backend_target::gate_state::complete &&
			    backend_target_frame.attempts == 1 && backend_target_frame.completions == 1 &&
			    backend_target_frame.failures == 0 && backend_target_frame.select_calls != 0 &&
			    backend_target_frame.view_copy_events == backend_view.copy_calls &&
			    backend_target_frame.unique_targets != 0 && backend_target_frame.invalid_observations == 0 &&
			    backend_target_frame.application_mismatches == 0 &&
			    backend_target_frame.entry_mutations == 0 && backend_target_frame.route_overflows == 0 &&
			    backend_target_frame.device_generation_mismatches == 0 &&
			    backend_target_frame.device_generation != 0 &&
			    backend_target_frame.start_present_post_frame != 0 &&
			    backend_target_frame.end_present_pre_frame ==
			        backend_target_frame.start_present_post_frame + 1 &&
			    backend_target_frame.start_present_post_thread_id != 0 &&
			    backend_target_frame.start_present_post_thread_id ==
			        backend_target_frame.boundary_thread_id &&
			    backend_target_frame_report_consistent && backend_target_frame_manifest_complete;
			const auto output_merger = engine_stereo_output_merger::get_status();
			std::unique_ptr<engine_stereo_output_merger::report> output_merger_report;
			bool output_merger_report_ready{};
			if (output_merger.state == engine_stereo_output_merger::gate_state::complete ||
			    output_merger.state == engine_stereo_output_merger::gate_state::failed)
			{
				output_merger_report = std::make_unique<engine_stereo_output_merger::report>();
				output_merger_report_ready = engine_stereo_output_merger::read_report(*output_merger_report);
			}
			const auto output_merger_manifest =
			    output_merger_report_ready
			        ? format_backend_output_merger(*output_merger_report, output_merger)
			        : std::string{};
			const auto output_merger_manifest_complete =
			    write_evidence_text_once(persistence.backend_output_merger_manifest_written,
			                             "backend-output-merger-manifest.txt",
			                             output_merger_manifest);
			const auto output_merger_report_consistent =
			    output_merger_report_ready &&
			    output_merger_report->expected_context == output_merger.expected_context &&
			    output_merger_report->device_generation == output_merger.device_generation &&
			    output_merger_report->publication_sequence == output_merger.latest_publication_sequence &&
			    output_merger_report->record == output_merger.latest_record &&
			    output_merger_report->transaction_thread_id != 0 &&
			    output_merger_report->event_count == output_merger.bind_calls;
			const auto output_merger_complete =
			    output_merger.hook_installed && output_merger.extended_hooks_installed &&
			    output_merger.hook_target != 0 && output_merger.unordered_access_hook_target != 0 &&
			    output_merger.clear_state_hook_target != 0 && output_merger.expected_context != 0 &&
			    output_merger.device_generation != 0 && output_merger.hook_failures == 0 &&
			    output_merger.state == engine_stereo_output_merger::gate_state::complete &&
			    output_merger.attempts == 1 && output_merger.completions == 1 &&
			    output_merger.failures == 0 && output_merger.bind_calls != 0 &&
			    output_merger.nonnull_bind_calls != 0 && output_merger.context_mismatches == 0 &&
			    output_merger.thread_mismatches == 0 && output_merger.query_failures == 0 &&
			    output_merger.overflows == 0 && output_merger.metadata_queries != 0 &&
			    output_merger_report_consistent && output_merger_manifest_complete;
			const auto draw_indexed = engine_stereo_draw_indexed::get_status();
			std::unique_ptr<engine_stereo_draw_indexed::report> draw_indexed_report;
			bool draw_indexed_report_ready{};
			if (draw_indexed.state == engine_stereo_draw_indexed::gate_state::complete ||
			    draw_indexed.state == engine_stereo_draw_indexed::gate_state::failed)
			{
				draw_indexed_report = std::make_unique<engine_stereo_draw_indexed::report>();
				draw_indexed_report_ready = engine_stereo_draw_indexed::read_report(*draw_indexed_report);
			}
			const auto draw_indexed_manifest =
			    draw_indexed_report_ready ? format_backend_draw_indexed(*draw_indexed_report, draw_indexed)
			                              : std::string{};
			const auto draw_indexed_manifest_complete =
			    write_evidence_text_once(persistence.backend_draw_indexed_manifest_written,
			                             "backend-draw-indexed-manifest.txt",
			                             draw_indexed_manifest);
			const auto draw_indexed_report_consistent =
			    draw_indexed_report_ready &&
			    draw_indexed_report->expected_context == draw_indexed.expected_context &&
			    draw_indexed_report->device_generation == draw_indexed.device_generation &&
			    draw_indexed_report->publication_sequence == draw_indexed.latest_publication_sequence &&
			    draw_indexed_report->record == draw_indexed.latest_record &&
			    draw_indexed_report->transaction_thread_id != 0 &&
			    draw_indexed_report->event_count == draw_indexed.draw_calls;
			const auto draw_indexed_complete =
			    draw_indexed.hook_installed && draw_indexed.hook_target != 0 &&
			    draw_indexed.expected_context != 0 && draw_indexed.device_generation != 0 &&
			    draw_indexed.hook_failures == 0 &&
			    draw_indexed.state == engine_stereo_draw_indexed::gate_state::complete &&
			    draw_indexed.attempts == 1 && draw_indexed.completions == 1 && draw_indexed.failures == 0 &&
			    draw_indexed.draw_calls != 0 && draw_indexed.context_mismatches == 0 &&
			    draw_indexed.thread_mismatches == 0 && draw_indexed.invalid_arguments == 0 &&
			    draw_indexed.overflows == 0 && draw_indexed_report_consistent &&
			    draw_indexed_manifest_complete;
			const auto execution = engine_stereo_execution::get_status();
			std::unique_ptr<engine_stereo_execution::report> execution_report;
			bool execution_report_ready{};
			const auto execution_terminal =
			    execution.state == engine_stereo_execution::gate_state::complete ||
			    execution.state == engine_stereo_execution::gate_state::failed;
			if (execution_terminal)
			{
				execution_report = std::make_unique<engine_stereo_execution::report>();
				execution_report_ready = engine_stereo_execution::read_report(*execution_report);
			}
			const auto execution_manifest = execution_report_ready
			                                    ? format_backend_execution(*execution_report, execution)
			                                    : std::string{};
			const auto execution_manifest_signature =
			    execution_manifest.empty()
			        ? 0
			        : content_signature(reinterpret_cast<std::uintptr_t>(execution_manifest.data()),
			                            execution_manifest.size());
			const auto execution_manifest_complete =
			    write_evidence_text_versioned(persistence.backend_execution_manifest_signature,
			                                  "backend-execution-census-manifest.txt",
			                                  execution_manifest,
			                                  execution_manifest_signature);
			std::uint64_t execution_total_events{};
			std::uint64_t execution_classifier_total{};
			std::uint64_t execution_frame_total{};
			bool execution_targets_complete =
			    execution.targets_distinct && execution.output_merger_target != 0 &&
			    execution.output_merger_unordered_access_target != 0 && execution.clear_state_target != 0;
			for (std::size_t index{}; index < engine_stereo_execution::api_count; ++index)
			{
				execution_total_events += execution.per_api[index];
				execution_classifier_total += execution.classifier_per_api[index];
				execution_frame_total += execution.frame_per_api[index];
				execution_targets_complete = execution_targets_complete && execution.hook_targets[index] != 0;
			}
			const auto execution_counts_consistent =
			    execution.event_reservations == execution.recorded_events &&
			    execution_total_events == execution.recorded_events &&
			    execution_classifier_total == execution.classifier_events &&
			    execution_frame_total == execution.frame_events &&
			    execution.recorded_events == execution.classifier_events + execution.frame_events &&
			    execution.backend_scoped_events <= execution.recorded_events &&
			    execution.backend_scoped_frame_events + execution.backend_unscoped_frame_events ==
			        execution.frame_events &&
			    execution.backend_scoped_frame_events <= execution.backend_scoped_events &&
			    execution.scene_owner_scoped_events <= execution.recorded_events &&
			    execution.scene_owner_scoped_frame_events + execution.scene_owner_unscoped_frame_events ==
			        execution.frame_events &&
			    execution.scene_owner_scoped_frame_events <= execution.scene_owner_scoped_events &&
			    execution.call_stack_samples <= execution.recorded_events;
			const auto execution_frame_boundary_complete =
			    execution_report_ready && execution.frame_start_present_post != 0 &&
			    execution.frame_end_present_pre == execution.frame_start_present_post + 1 &&
			    execution.frame_end_present_post == execution.frame_end_present_pre &&
			    SUCCEEDED(execution.frame_present_result) && execution_report->frame_start_thread_id != 0 &&
			    execution_report->frame_start_thread_id == execution_report->frame_end_thread_id;
			const auto execution_report_consistent =
			    execution_report_ready && execution_report->expected_context == execution.expected_context &&
			    execution_report->device_generation == execution.device_generation &&
			    execution_report->classifier_record == execution.classifier_record &&
			    execution_report->classifier_record_type == execution.classifier_record_type &&
			    execution_report->classifier_thread_id == execution.classifier_thread_id &&
			    execution_report->classifier_begin_qpc == execution.classifier_begin_qpc &&
			    execution_report->classifier_end_qpc == execution.classifier_end_qpc &&
			    execution_report->frame_start_present_post == execution.frame_start_present_post &&
			    execution_report->frame_end_present_pre == execution.frame_end_present_pre &&
			    execution_report->frame_end_present_post == execution.frame_end_present_post &&
			    execution_report->frame_present_result == execution.frame_present_result &&
			    execution_report->event_count == execution.recorded_events &&
			    execution_report->classifier_event_count == execution.classifier_events &&
			    execution_report->frame_event_count == execution.frame_events &&
			    execution_report->expected_context_frame_events == execution.expected_context_frame_events &&
			    execution_report->backend_scoped_events == execution.backend_scoped_events &&
			    execution_report->backend_scoped_frame_events == execution.backend_scoped_frame_events &&
			    execution_report->backend_unscoped_frame_events == execution.backend_unscoped_frame_events &&
			    execution_report->backend_thread_mismatches == execution.backend_thread_mismatches &&
			    execution_report->distinct_backend_records == execution.distinct_backend_records &&
			    execution_report->scene_owner_scoped_events == execution.scene_owner_scoped_events &&
			    execution_report->scene_owner_scoped_frame_events ==
			        execution.scene_owner_scoped_frame_events &&
			    execution_report->scene_owner_unscoped_frame_events ==
			        execution.scene_owner_unscoped_frame_events &&
			    execution_report->scene_owner_thread_mismatches == execution.scene_owner_thread_mismatches &&
			    execution_report->distinct_scene_owner_records == execution.distinct_scene_owner_records &&
			    execution_report->call_stack_samples == execution.call_stack_samples &&
			    execution_report->call_stack_capture_failures == execution.call_stack_capture_failures &&
			    execution_report->call_stack_key_overflows == execution.call_stack_key_overflows &&
			    execution_report->admission_collisions == execution.admission_collisions &&
			    execution_report->foreign_context_events == execution.foreign_context_events &&
			    execution_report->known_conversion_recordings == execution.known_conversion_recordings &&
			    execution_report->known_conversion_executions == execution.known_conversion_executions &&
			    execution_report->known_conversion_replays == execution.known_conversion_replays &&
			    execution_report->per_api == execution.per_api &&
			    execution_report->classifier_per_api == execution.classifier_per_api &&
			    execution_report->frame_per_api == execution.frame_per_api &&
			    execution_report->deferred_execution_opaque == execution.deferred_execution_opaque &&
			    execution_report->identity_truncated == execution.identity_truncated;
			const auto observation_device_identity_consistent =
			    backend_target_frame.device_generation != 0 &&
			    backend_target_frame.device_generation == output_merger.device_generation &&
			    backend_target_frame.device_generation == draw_indexed.device_generation &&
			    backend_target_frame.device_generation == execution.device_generation &&
			    output_merger.expected_context != 0 &&
			    output_merger.expected_context == draw_indexed.expected_context &&
			    output_merger.expected_context == execution.expected_context &&
			    output_merger_report_consistent && draw_indexed_report_consistent &&
			    backend_target_frame_report_consistent;
			const auto terminal_device_identity_consistent =
			    observation_device_identity_consistent && execution_report_consistent;
			const auto observation_complete =
			    all_ready && files_complete && summaries_complete && binding_complete &&
			    backend_view_complete && backend_target_complete && backend_target_frame_complete &&
			    output_merger_complete && draw_indexed_complete && observation_device_identity_consistent;
			const auto execution_census_complete =
			    execution.hooks_installed && !execution.installation_permanently_failed &&
			    execution.installed_hook_count == engine_stereo_execution::api_count - 1 &&
			    execution_targets_complete && execution.hook_failures == 0 &&
			    execution.state == engine_stereo_execution::gate_state::complete &&
			    execution.error == engine_stereo_execution::failure::none && execution.attempts == 1 &&
			    execution.completions == 1 && execution.failures == 0 && execution.recorded_events != 0 &&
			    execution.draw_indexed_forwarding_external && execution.draw_indexed_forwarded_calls != 0 &&
			    execution.classifier_events != 0 && execution.frame_events != 0 &&
			    execution.expected_context_frame_events != 0 && execution.backend_scoped_frame_events != 0 &&
			    execution.distinct_backend_records != 0 && execution.backend_thread_mismatches == 0 &&
			    execution.scene_owner_scoped_frame_events != 0 &&
			    execution.distinct_scene_owner_records != 0 && execution.scene_owner_thread_mismatches == 0 &&
			    execution.call_stack_samples != 0 && execution.call_stack_capture_failures == 0 &&
			    execution.call_stack_key_overflows == 0 && execution.foreign_context_events == 0 &&
			    execution.overflows == 0 && execution.admission_collisions == 0 &&
			    execution.classifier_thread_mismatches == 0 && !execution.deferred_execution_opaque &&
			    execution.opaque_execute_command_lists == 0 && !execution.identity_truncated &&
			    execution_counts_consistent &&
			    execution.classifier_record_type == native_render_contract::expected_world_record_type &&
			    execution.classifier_begin_qpc != 0 &&
			    execution.classifier_end_qpc >= execution.classifier_begin_qpc &&
			    execution_frame_boundary_complete && execution_report_consistent &&
			    execution_manifest_complete;
			const auto execution_complete = accepts_complete_execution_evidence({
			    observation_complete,
			    backend_target_frame_complete,
			    output_merger_complete,
			    draw_indexed_complete,
			    execution_census_complete,
			    terminal_device_identity_consistent,
			});
			std::ostringstream manifest;
			const char* manifest_state = "waiting_for_level";
			if (observation_complete)
				manifest_state = "execution_pending";
			if (execution_terminal)
				manifest_state = "incomplete";
			if (execution_terminal && execution.deferred_execution_opaque)
			{
				manifest_state = "opaque";
			}
			if (execution.state == engine_stereo_execution::gate_state::failed)
			{
				manifest_state = "failed";
			}
			if (execution_complete)
				manifest_state = "complete";
			manifest
			    << "state=" << manifest_state << "\r\n"
			    << "contract=bounded_h2_observation_then_read_only_d3d11_execution_census\r\n"
			    << "renderer_thread_file_io=none\r\n"
			    << "observation_additional_d3d_gpu_commands=0\r\n"
			    << "stereo_replay_probe=retired\r\n"
			    << "execution_census_additional_gpu_commands=0\r\n"
			    << "openvr_calls=0\r\nrecord_mutations=0\r\n"
			    << "stereo_slot_contract=record_local_center_view_derivation\r\n"
			    << "stereo_slot_h2_pure_finalizer_calls_per_family=2\r\n"
			    << "stereo_slot_capture_state="
			    << evidence::captures.stereo_eye_slot_capture_state.load(std::memory_order_acquire) << "\r\n"
			    << "backend_binding_contract=immutable_cpu_claim_only\r\n"
			    << "backend_binding_state=" << (binding_complete ? "complete" : "waiting")
			    << " publications=" << binding.publications << " claims=" << binding.claims
			    << " releases=" << binding.releases << " publication_drops=" << binding.publication_drops
			    << " mapping_misses=" << binding.mapping_misses
			    << " invalid_publications=" << binding.invalid_publications
			    << " release_mismatches=" << binding.release_mismatches
			    << " active_claims=" << binding.active_claims
			    << " max_active_claims=" << binding.maximum_active_claims << "\r\n"
			    << "backend_eye_view_copy_contract=one_shot_left_eye_exact_0x170\r\n"
			    << "backend_eye_view_copy_state=" << engine_stereo_backend_view::to_string(backend_view.state)
			    << " attempts=" << backend_view.attempts << " completions=" << backend_view.completions
			    << " failures=" << backend_view.failures << " copy_calls=" << backend_view.copy_calls
			    << " substitutions=" << backend_view.substitutions
			    << " foreign_copy_bypasses=" << backend_view.foreign_copy_bypasses
			    << " destination_mismatches=" << backend_view.destination_mismatches
			    << " record_mutations=" << backend_view.record_mutations
			    << " dispatch_misses=" << backend_view.dispatch_misses
			    << " publication_sequence=" << backend_view.latest_publication_sequence << " record=0x"
			    << std::hex << backend_view.latest_record << std::dec << "\r\n"
			    << "backend_target_route_contract=one_shot_cpu_observation_only\r\n"
			    << "backend_target_route_state="
			    << engine_stereo_backend_target::to_string(backend_target.state)
			    << " attempts=" << backend_target.attempts << " completions=" << backend_target.completions
			    << " failures=" << backend_target.failures << " select_calls=" << backend_target.select_calls
			    << " dispatch_select_calls=" << backend_target.dispatch_select_calls
			    << " applied_transitions=" << backend_target.applied_transitions
			    << " retained_targets=" << backend_target.retained_targets
			    << " unique_targets=" << backend_target.unique_targets
			    << " invalid_observations=" << backend_target.invalid_observations
			    << " application_mismatches=" << backend_target.application_mismatches
			    << " entry_mutations=" << backend_target.entry_mutations
			    << " route_overflows=" << backend_target.route_overflows
			    << " report_written=" << (backend_target_manifest_complete ? "yes" : "no")
			    << " publication_sequence=" << backend_target.latest_publication_sequence << " record=0x"
			    << std::hex << backend_target.latest_record << std::dec << "\r\n"
			    << "backend_target_frame_contract=claim_to_following_present_pre_cpu_observation\r\n"
			    << "backend_target_frame_state="
			    << engine_stereo_backend_target::to_string(backend_target_frame.state)
			    << " attempts=" << backend_target_frame.attempts
			    << " completions=" << backend_target_frame.completions
			    << " failures=" << backend_target_frame.failures
			    << " select_calls=" << backend_target_frame.select_calls
			    << " view_copy_events=" << backend_target_frame.view_copy_events
			    << " unique_targets=" << backend_target_frame.unique_targets
			    << " invalid_observations=" << backend_target_frame.invalid_observations
			    << " application_mismatches=" << backend_target_frame.application_mismatches
			    << " entry_mutations=" << backend_target_frame.entry_mutations
			    << " route_overflows=" << backend_target_frame.route_overflows
			    << " generation_mismatches=" << backend_target_frame.device_generation_mismatches
			    << " max_concurrent_writers=" << backend_target_frame.maximum_active_writers
			    << " start_present_post=" << backend_target_frame.start_present_post_frame
			    << " start_present_post_thread=" << backend_target_frame.start_present_post_thread_id
			    << " end_present_pre=" << backend_target_frame.end_present_pre_frame
			    << " device_generation=" << backend_target_frame.device_generation
			    << " boundary_thread=" << backend_target_frame.boundary_thread_id
			    << " report_written=" << (backend_target_frame_manifest_complete ? "yes" : "no")
			    << " publication_sequence=" << backend_target_frame.start_publication_sequence << " record=0x"
			    << std::hex << backend_target_frame.start_record << std::dec << "\r\n"
			    << "backend_output_merger_contract=one_shot_read_only_exact_transaction\r\n"
			    << "backend_output_merger_state="
			    << engine_stereo_output_merger::to_string(output_merger.state)
			    << " hook_installed=" << (output_merger.hook_installed ? "yes" : "no")
			    << " extended_hooks_installed=" << (output_merger.extended_hooks_installed ? "yes" : "no")
			    << " hook_target=0x" << std::hex << output_merger.hook_target << " uav_hook_target=0x"
			    << output_merger.unordered_access_hook_target << " clear_state_hook_target=0x"
			    << output_merger.clear_state_hook_target << " context=0x" << output_merger.expected_context
			    << std::dec << " generation=" << output_merger.device_generation
			    << " hook_failures=" << output_merger.hook_failures << " attempts=" << output_merger.attempts
			    << " completions=" << output_merger.completions << " failures=" << output_merger.failures
			    << " bind_calls=" << output_merger.bind_calls
			    << " nonnull_bind_calls=" << output_merger.nonnull_bind_calls
			    << " context_mismatches=" << output_merger.context_mismatches
			    << " thread_mismatches=" << output_merger.thread_mismatches
			    << " query_failures=" << output_merger.query_failures
			    << " overflows=" << output_merger.overflows
			    << " metadata_queries=" << output_merger.metadata_queries
			    << " unordered_access_calls=" << output_merger.unordered_access_calls
			    << " clear_state_calls=" << output_merger.clear_state_calls
			    << " binding_invalidations=" << output_merger.binding_invalidations
			    << " report_written=" << (output_merger_manifest_complete ? "yes" : "no")
			    << " publication_sequence=" << output_merger.latest_publication_sequence << " record=0x"
			    << std::hex << output_merger.latest_record << std::dec << "\r\n"
			    << "backend_draw_indexed_contract=one_shot_read_only_exact_dispatch\r\n"
			    << "backend_draw_indexed_state=" << engine_stereo_draw_indexed::to_string(draw_indexed.state)
			    << " hook_installed=" << (draw_indexed.hook_installed ? "yes" : "no") << " hook_target=0x"
			    << std::hex << draw_indexed.hook_target << " context=0x" << draw_indexed.expected_context
			    << std::dec << " generation=" << draw_indexed.device_generation
			    << " hook_failures=" << draw_indexed.hook_failures << " attempts=" << draw_indexed.attempts
			    << " completions=" << draw_indexed.completions << " failures=" << draw_indexed.failures
			    << " draw_calls=" << draw_indexed.draw_calls
			    << " context_mismatches=" << draw_indexed.context_mismatches
			    << " thread_mismatches=" << draw_indexed.thread_mismatches
			    << " invalid_arguments=" << draw_indexed.invalid_arguments
			    << " overflows=" << draw_indexed.overflows
			    << " report_written=" << (draw_indexed_manifest_complete ? "yes" : "no")
			    << " publication_sequence=" << draw_indexed.latest_publication_sequence << " record=0x"
			    << std::hex << draw_indexed.latest_record << std::dec << "\r\n"
			    << "backend_execution_contract=exact_type4_classifier_plus_next_present_to_present_frame_with_outer_record_owner_read_only\r\n"
			    << "backend_execution_state=" << engine_stereo_execution::to_string(execution.state)
			    << " error=" << engine_stereo_execution::to_string(execution.error)
			    << " hooks_installed=" << (execution.hooks_installed ? "yes" : "no")
			    << " hook_count=" << execution.installed_hook_count
			    << " targets_distinct=" << (execution.targets_distinct ? "yes" : "no")
			    << " permanent_install_failure=" << (execution.installation_permanently_failed ? "yes" : "no")
			    << " draw_indexed_forwarding_external="
			    << (execution.draw_indexed_forwarding_external ? "yes" : "no")
			    << " draw_indexed_forwarded_calls=" << execution.draw_indexed_forwarded_calls << " context=0x"
			    << std::hex << execution.expected_context << std::dec
			    << " generation=" << execution.device_generation
			    << " hook_failures=" << execution.hook_failures << " attempts=" << execution.attempts
			    << " completions=" << execution.completions << " failures=" << execution.failures
			    << " reservations=" << execution.event_reservations << " events=" << execution.recorded_events
			    << " classifier_events=" << execution.classifier_events
			    << " frame_events=" << execution.frame_events
			    << " expected_context_frame_events=" << execution.expected_context_frame_events
			    << " backend_scoped_events=" << execution.backend_scoped_events
			    << " backend_scoped_frame_events=" << execution.backend_scoped_frame_events
			    << " backend_unscoped_frame_events=" << execution.backend_unscoped_frame_events
			    << " backend_thread_mismatches=" << execution.backend_thread_mismatches
			    << " distinct_backend_records=" << execution.distinct_backend_records
			    << " scene_owner_scoped_events=" << execution.scene_owner_scoped_events
			    << " scene_owner_scoped_frame_events=" << execution.scene_owner_scoped_frame_events
			    << " scene_owner_unscoped_frame_events=" << execution.scene_owner_unscoped_frame_events
			    << " scene_owner_thread_mismatches=" << execution.scene_owner_thread_mismatches
			    << " distinct_scene_owner_records=" << execution.distinct_scene_owner_records
			    << " call_stack_samples=" << execution.call_stack_samples
			    << " call_stack_capture_failures=" << execution.call_stack_capture_failures
			    << " call_stack_key_overflows=" << execution.call_stack_key_overflows
			    << " admission_collisions=" << execution.admission_collisions
			    << " overflows=" << execution.overflows
			    << " foreign_context_events=" << execution.foreign_context_events
			    << " execute_command_lists=" << execution.opaque_execute_command_lists
			    << " known_conversion_recordings=" << execution.known_conversion_recordings
			    << " known_conversion_executions=" << execution.known_conversion_executions
			    << " known_conversion_replays=" << execution.known_conversion_replays
			    << " deferred_execution_opaque=" << (execution.deferred_execution_opaque ? "yes" : "no")
			    << " identity_truncated=" << (execution.identity_truncated ? "yes" : "no")
			    << " classifier_thread_mismatches=" << execution.classifier_thread_mismatches
			    << " frame=" << execution.frame_start_present_post << "->" << execution.frame_end_present_pre
			    << "->" << execution.frame_end_present_post << " present_result=0x" << std::hex
			    << static_cast<std::uint32_t>(execution.frame_present_result) << std::dec
			    << " report_written=" << (execution_manifest_complete ? "yes" : "no")
			    << " classifier_record=0x" << std::hex << execution.classifier_record << std::dec
			    << " classifier_type=" << execution.classifier_record_type << " immutable_identity_hash=0x"
			    << std::hex << execution_manifest_signature << std::dec << "\r\n"
			    << "backend_stereo_replay_contract=retired_no_gpu_work\r\n"
			    << "metadata_schema=artifact-specific; m0=tick and pointer fields are runtime-only\r\n"
			    << "baseline_menu_gate=baseline m2 is target_prepare_calls and must be zero\r\n";
			append_artifact_manifest(manifest,
			                         "target_registry_baseline",
			                         "target-registry-baseline.bin",
			                         evidence::captures.baseline_target_registry_artifact);
			append_artifact_manifest(manifest,
			                         "scene_descriptor_before",
			                         "scene-descriptor-before.bin",
			                         evidence::captures.descriptor_before_artifact);
			append_artifact_manifest(manifest,
			                         "scene_descriptor_after",
			                         "scene-descriptor-after.bin",
			                         evidence::captures.descriptor_after_artifact);
			append_artifact_manifest(manifest,
			                         "view_slot_before_initializer",
			                         "view-slot-before-initializer.bin",
			                         evidence::captures.slot_before_initializer_artifact);
			append_artifact_manifest(manifest,
			                         "view_slot_after_initializer",
			                         "view-slot-after-initializer.bin",
			                         evidence::captures.slot_after_initializer_artifact);
			append_artifact_manifest(manifest,
			                         "stereo_eye_left_slot",
			                         "stereo-eye-left-slot.bin",
			                         evidence::captures.stereo_eye_slot_artifacts[0]);
			append_artifact_manifest(manifest,
			                         "stereo_eye_right_slot",
			                         "stereo-eye-right-slot.bin",
			                         evidence::captures.stereo_eye_slot_artifacts[1]);
			append_artifact_manifest(manifest,
			                         "backend_view_source_before",
			                         "backend-view-source-before.bin",
			                         evidence::captures.backend_view_source_before_artifact);
			append_artifact_manifest(manifest,
			                         "backend_view_source_after",
			                         "backend-view-source-after.bin",
			                         evidence::captures.backend_view_source_after_artifact);
			append_artifact_manifest(manifest,
			                         "backend_bound_left_slot",
			                         "backend-bound-left-slot.bin",
			                         evidence::captures.backend_bound_eye_slot_artifacts[0]);
			append_artifact_manifest(manifest,
			                         "backend_bound_right_slot",
			                         "backend-bound-right-slot.bin",
			                         evidence::captures.backend_bound_eye_slot_artifacts[1]);
			append_artifact_manifest(manifest,
			                         "view_slot_after_generator",
			                         "view-slot-after-generator.bin",
			                         evidence::captures.slot_after_generator_artifact);
			append_artifact_manifest(manifest,
			                         "per_client_output_after_generator",
			                         "per-client-output-after-generator.bin",
			                         evidence::captures.output_after_generator_artifact);
			append_artifact_manifest(manifest,
			                         "backend_record_before_target_prepare",
			                         "backend-record-before-target-prepare.bin",
			                         evidence::captures.record_before_target_prepare_artifact);
			append_artifact_manifest(manifest,
			                         "backend_record_after_target_prepare",
			                         "backend-record-after-target-prepare.bin",
			                         evidence::captures.record_after_target_prepare_artifact);
			append_artifact_manifest(manifest,
			                         "target_registry_before_target_prepare",
			                         "target-registry-before-target-prepare.bin",
			                         evidence::captures.registry_before_target_prepare_artifact);
			append_artifact_manifest(manifest,
			                         "target_registry_after_target_prepare",
			                         "target-registry-after-target-prepare.bin",
			                         evidence::captures.registry_after_target_prepare_artifact);
			append_delta_manifest(manifest,
			                      "scene_descriptor",
			                      evidence::captures.descriptor_before_artifact,
			                      evidence::captures.descriptor_after_artifact);
			append_delta_manifest(manifest,
			                      "view_slot_initializer",
			                      evidence::captures.slot_before_initializer_artifact,
			                      evidence::captures.slot_after_initializer_artifact);
			append_delta_manifest(manifest,
			                      "natural_to_left_eye_slot",
			                      evidence::captures.slot_after_initializer_artifact,
			                      evidence::captures.stereo_eye_slot_artifacts[0]);
			append_delta_manifest(manifest,
			                      "natural_to_right_eye_slot",
			                      evidence::captures.slot_after_initializer_artifact,
			                      evidence::captures.stereo_eye_slot_artifacts[1]);
			append_delta_manifest(manifest,
			                      "backend_view_source_stability",
			                      evidence::captures.backend_view_source_before_artifact,
			                      evidence::captures.backend_view_source_after_artifact);
			append_delta_manifest(manifest,
			                      "backend_source_to_bound_left",
			                      evidence::captures.backend_view_source_before_artifact,
			                      evidence::captures.backend_bound_eye_slot_artifacts[0]);
			append_delta_manifest(manifest,
			                      "backend_source_to_bound_right",
			                      evidence::captures.backend_view_source_before_artifact,
			                      evidence::captures.backend_bound_eye_slot_artifacts[1]);
			append_delta_manifest(manifest,
			                      "backend_target_prepare_record",
			                      evidence::captures.record_before_target_prepare_artifact,
			                      evidence::captures.record_after_target_prepare_artifact);
			append_delta_manifest(manifest,
			                      "target_registry_during_prepare",
			                      evidence::captures.registry_before_target_prepare_artifact,
			                      evidence::captures.registry_after_target_prepare_artifact);
			const auto manifest_bytes = manifest.str();
			const auto manifest_signature =
			    manifest_bytes.empty()
			        ? 0
			        : content_signature(reinterpret_cast<std::uintptr_t>(manifest_bytes.data()),
			                            manifest_bytes.size());
			// Version and persist the exact final bytes. The manifest embeds the full
			// execution-report hash above, so a current-process write is tied to the
			// immutable terminal census rather than a repeating readiness pattern.
			const auto aggregate_manifest_complete = write_evidence_text_versioned(
			    persistence.artifact_manifest_signature, "manifest.txt", manifest_bytes, manifest_signature);

			if (observation_complete &&
			    !persistence.artifact_completion_announced.exchange(true, std::memory_order_acq_rel))
			{
				console::info(
				    "[VR] backend observation evidence complete; read-only full-frame D3D11 execution census is active\n");
			}
			if (execution_terminal &&
			    !persistence.backend_execution_completion_announced.exchange(true, std::memory_order_acq_rel))
			{
				if (execution.state == engine_stereo_execution::gate_state::failed)
				{
					console::error(
					    "[VR] full-frame D3D11 execution census failed: %s; automatic status persistence is pending\n",
					    engine_stereo_execution::to_string(execution.error));
				}
				else if (execution.deferred_execution_opaque)
				{
					console::error(
					    "[VR] full-frame D3D11 execution census is opaque and unacceptable; automatic status persistence is pending\n");
				}
				else if (execution_complete)
				{
					console::info(
					    "[VR] full-frame D3D11 execution census complete; automatic status persistence is pending\n");
				}
				else
				{
					console::error(
					    "[VR] full-frame D3D11 execution census reached a terminal state but strict acceptance rejected incomplete evidence; automatic status persistence is pending\n");
				}
			}

			// The pre-R_EndFrame dual-record mutation is retired. Same-process A/B
			// evidence showed that the one frame in which it ran caused H2's
			// "Tried to use (null)" fatal drop, while the next level entry in the
			// same process succeeded after the one-shot state stopped mutating H2.
			// Replace the failed experiment's artifact writer with an explicit retired
			// manifest so stale files cannot be mistaken for a runtime path.
			if (!persistence.retired_dual_record_manifest_written.load(std::memory_order_acquire))
			{
				const std::string retired_manifest =
				    "state=retired\r\n"
				    "contract=none\r\n"
				    "frontend_mutation=disabled\r\n"
				    "gpu_transport=hard_unarmed\r\n"
				    "reason=same_process_A_B_proved_pre_R_EndFrame_scene_synthesis_caused_H2_fatal_drop\r\n"
				    "replacement=record_local_stereo_view_slot_derivation\r\n";
				if (utils::io::write_file_atomic(
				        std::string{evidence_directory} + "/dual-record-manifest.txt", retired_manifest))
				{
					persistence.retired_dual_record_manifest_written.store(true, std::memory_order_release);
				}
			}
			if (!persistence.retired_owner_stereo_manifest_written.load(std::memory_order_acquire))
			{
				const std::string retired_manifest =
				    "state=retired\r\n"
				    "contract=none\r\n"
				    "activation_command=removed\r\n"
				    "scene_owner_detour=removed\r\n"
				    "gpu_transport=hard_unarmed\r\n"
				    "reason=same_build_A_B_proved_second_owner_call_corrupts_surface_builder_transient_state\r\n"
				    "replacement=record_local_stereo_view_slot_derivation\r\n";
				if (utils::io::write_file_atomic(
				        std::string{evidence_directory} + "/owner-stereo-manifest.txt", retired_manifest))
				{
					persistence.retired_owner_stereo_manifest_written.store(true, std::memory_order_release);
				}
			}

			return execution_terminal && execution_report_ready && execution_manifest_complete &&
			       aggregate_manifest_complete;
		}
		catch (...)
		{
			// Evidence persistence is diagnostic-only. Never let control-plane I/O
			// unwind into the monitoring thread or alter H2/OpenVR execution.
			return false;
		}
	}
}
