#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "../engine_backend_probe.hpp"
#include "../engine_scene_completion.hpp"
#include "../engine_stereo_backend_target.hpp"
#include "../engine_stereo_backend_view.hpp"
#include "../engine_stereo_binding.hpp"
#include "../engine_stereo_bridge.hpp"
#include "../engine_stereo_execution.hpp"
#include "../engine_stereo_renderer.hpp"
#include "../engine_view_probe.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vr::diagnostics::detail
{
	void append_frontend_status(std::ostringstream& output,
		const engine_stereo_bridge::status& bridge_status,
		const engine_view_probe::status& view_probe_status,
		const engine_backend_probe::status& backend_probe_status,
		const engine_stereo_binding::status& stereo_binding_status,
		const engine_stereo_backend_view::status& backend_view_status,
		const engine_stereo_backend_target::status& backend_target_status,
		const engine_stereo_backend_target::frame_status& backend_target_frame_status,
		const engine_stereo_renderer::culling_union_status& culling_status)
	{
		output << "engine stereo bridge:\n";
		output << "  observer_policy: backend_bootstrap=" <<
			(engine_stereo_execution::bootstrap_complete() ? "retired" : "pending");
		output << " investigation_hooks=removed pointer_cache=direct_mapped_1024\n";
		output << "  target_matched=" << yes_no(bridge_status.target_matched);
		output << " enabled=" << yes_no(bridge_status.enabled);
		output << " views_available=" << yes_no(bridge_status.views_available);
		output << " render_hook=" << yes_no(bridge_status.render_hook_installed);
		output << " swap_eyes=" << yes_no(bridge_status.swap_eyes) << '\n';
		const auto preflight_ok = bridge_status.target_matched &&
			bridge_status.render_hook_installed;
		output << "  strict_preflight=" << (preflight_ok ? "ready" : "blocked");
		if (!bridge_status.target_matched)
		{
			output << " reason=target_identity";
		}
		else if (!bridge_status.render_hook_installed)
		{
			output << " reason=scene_hook";
		}
		else if (!bridge_status.views_available)
		{
			output << " reason=waiting_for_view_family";
		}
		output << '\n';
		output << "  culling_union: attempts=" << culling_status.attempts;
		output << " applied=" << culling_status.applications;
		output << " failures=" << culling_status.failures;
		output << " near=" << culling_status.near_distance_units;
		output << " horizontal_expansion=" << culling_status.horizontal_origin_expansion << '\n';
		output << "  culling_union_tangents=" << culling_status.tan_left << ',';
		output << culling_status.tan_right << ',' << culling_status.tan_down << ',';
		output << culling_status.tan_up << '\n';
		output << "  fx_culling_union: attempts=" << culling_status.fx_attempts;
		output << " applied=" << culling_status.fx_applications;
		output << " failures=" << culling_status.fx_failures << '\n';
		output << "  strict_scene_pair=";
		if (bridge_status.coherent_stereo_pairs != 0)
		{
			output << "observed";
		}
		else if (bridge_status.scene_calls == 0)
		{
			output << "not_observed";
		}
		else
		{
			output << "blocked";
		}
		output << " hook_entries=" << bridge_status.scene_hook_entries;
		output << " calls=" << bridge_status.scene_calls;
		output << " coherent=" << bridge_status.coherent_stereo_pairs;
		output << " incoherent=" << bridge_status.incoherent_stereo_pairs << '\n';
		output << "  cpu_view_probe=" << (view_probe_status.enabled ? "enabled" : "disabled");
		output << " transactions=" << view_probe_status.transaction_count;
		output << " records=" << view_probe_status.newest_sequence;
		output << " overwritten=" << view_probe_status.overwrite_count;
		output << " dropped=" << view_probe_status.dropped_records;
		output << " orphan=" << view_probe_status.orphan_records;
		output << " duplicate=" << view_probe_status.duplicate_records;
		output << " invalid_slot=" << view_probe_status.invalid_slot_records;
		output << " count_mismatch=" << view_probe_status.count_mismatch_records;
		output << " thread_mismatch=" << view_probe_status.thread_mismatch_records << '\n';
		output << "  cpu_view_state: snapshots=" << view_probe_status.view_state_snapshots;
		output << " slot_stable=" << view_probe_status.stable_slot_comparisons;
		output << " slot_changed=" << view_probe_status.changed_slot_comparisons;
		output << " output_copy_match=" << view_probe_status.matching_output_copies;
		output << " output_copy_mismatch="
			<< view_probe_status.mismatching_output_copies << '\n';
		output << "  cpu_slot_initializer: calls=" << view_probe_status.slot_initializer_calls;
		output << " slot_changed=" << view_probe_status.slot_initializer_slot_changes;
		output << " shared_changed=" << view_probe_status.slot_initializer_shared_changes;
		output << " invalid_slot=" << view_probe_status.slot_initializer_invalid_slots << '\n';
		output << "  cpu_descriptor_contract: samples="
			<< view_probe_status.descriptor_contract_samples;
		output << " stable=" << view_probe_status.descriptor_contract_stable;
		output << " changed=" << view_probe_status.descriptor_contract_changes;
		output << " unreadable=" << view_probe_status.descriptor_contract_unreadable << '\n';
		output << "  cpu_camera_state: snapshots=" << view_probe_status.camera_state_snapshots;
		output << " scoped=" << view_probe_status.scoped_camera_state_snapshots;
		output << " unscoped=" << view_probe_status.unscoped_camera_state_snapshots;
		output << " set_viewpos_calls=" << view_probe_status.set_viewpos_calls;
		output << " helper_calls=" << view_probe_status.camera_helper_calls << '\n';
		output << "  backend_probe=" << (backend_probe_status.enabled ? "enabled" : "disabled");
		output << " frontend_publications=" << backend_probe_status.frontend_publications;
		output << " claims=" << backend_probe_status.frontend_claims;
		output << " mapping_miss=" << backend_probe_status.frontend_mapping_misses;
		output << " mapping_drop=" << backend_probe_status.frontend_publication_drops;
		output << " unclaimed_overwrite="
			<< backend_probe_status.frontend_unclaimed_overwrites << '\n';
		output << "  backend_stereo_binding: publications="
			<< stereo_binding_status.publications;
		output << " claims=" << stereo_binding_status.claims;
		output << " releases=" << stereo_binding_status.releases;
		output << " publication_drops=" << stereo_binding_status.publication_drops;
		output << " mapping_misses=" << stereo_binding_status.mapping_misses;
		output << " invalid=" << stereo_binding_status.invalid_publications;
		output << " release_mismatch=" << stereo_binding_status.release_mismatches;
		output << " active=" << stereo_binding_status.active_claims;
		output << " max_active=" << stereo_binding_status.maximum_active_claims << '\n';
		output << "  owner_scene_binding: policy=newest_exact_source_camera matches="
			<< stereo_binding_status.current_scene_matches;
		output << " misses=" << stereo_binding_status.current_scene_misses;
		output << " superseded=" << stereo_binding_status.superseded_publications;
		output << " camera_mismatch=" << stereo_binding_status.current_scene_camera_mismatches;
		output << " camera_changed=" << stereo_binding_status.current_scene_camera_changes << '\n';
		const auto handoff = engine_stereo_renderer::get_scene_handoff_status();
		const auto completion = engine_scene_completion::get_status();
		output << "  scene_input_handoff: policy=views_before_generator_payload_after_cpu_completion attempts="
			<< handoff.attempts << " published=" << handoff.publications << " failures=" << handoff.failures << '\n';
		output << "  scene_input_completion: attempts=" << completion.attempts << " complete=" << completion.completions
			<< " pending=" << completion.pending << " failures=" << completion.failures
			<< " last_error=" << static_cast<unsigned>(completion.last_failure) << '\n';
		output << "  owner_camera_identity: scope=admitted_pre_eye model_center=published_scene comparison=exact_bytes matches="
			<< stereo_binding_status.owner_camera_matches;
		output << " differences=" << stereo_binding_status.owner_camera_differences;
		output << " invalid=" << stereo_binding_status.owner_camera_invalid << '\n';
		const auto& camera_difference = stereo_binding_status.first_owner_camera_difference;
		if (camera_difference.publication_sequence != 0)
		{
			output << "  owner_camera_first_difference: sequence="
				<< camera_difference.publication_sequence << " pair=" << camera_difference.pair_id;
			output << " epoch=" << camera_difference.frontend_epoch;
			output << " transaction=" << camera_difference.frontend_transaction_id;
			output << " frontend=0x" << std::hex << camera_difference.frontend;
			output << " record=0x" << camera_difference.record << std::dec << '\n';
			const auto saved_flags = output.flags();
			const auto saved_precision = output.precision();
			output << std::defaultfloat << std::setprecision(9);
			for (std::size_t group{}; group < 4; ++group)
			{
				const auto offset = group * 3;
				output << "    " << (group == 0 ? "origin" : "axis") << "["
					<< (group == 0 ? 0 : group - 1)
					<< "]: published=" << camera_difference.published[offset] << ','
					<< camera_difference.published[offset + 1] << ','
					<< camera_difference.published[offset + 2];
				output << " consumed=" << camera_difference.consumed[offset] << ','
					<< camera_difference.consumed[offset + 1] << ','
					<< camera_difference.consumed[offset + 2] << '\n';
			}
			output.flags(saved_flags);
			output.precision(saved_precision);
		}
		output << "  backend_eye_view_copy: state="
			<< engine_stereo_backend_view::to_string(backend_view_status.state);
		output << " attempts=" << backend_view_status.attempts;
		output << " complete=" << backend_view_status.completions;
		output << " failures=" << backend_view_status.failures;
		output << " calls=" << backend_view_status.copy_calls;
		output << " substitutions=" << backend_view_status.substitutions;
		output << " foreign_bypass=" << backend_view_status.foreign_copy_bypasses;
		output << " destination_mismatch=" << backend_view_status.destination_mismatches;
		output << " record_mutation=" << backend_view_status.record_mutations;
		output << " dispatch_miss=" << backend_view_status.dispatch_misses << '\n';
		output << "  backend_target_route: state="
			<< engine_stereo_backend_target::to_string(backend_target_status.state);
		output << " attempts=" << backend_target_status.attempts;
		output << " complete=" << backend_target_status.completions;
		output << " failures=" << backend_target_status.failures;
		output << " selects=" << backend_target_status.select_calls;
		output << " dispatch_selects=" << backend_target_status.dispatch_select_calls;
		output << " applied=" << backend_target_status.applied_transitions;
		output << " retained=" << backend_target_status.retained_targets;
		output << " unique=" << backend_target_status.unique_targets;
		output << " invalid=" << backend_target_status.invalid_observations;
		output << " apply_mismatch=" << backend_target_status.application_mismatches;
		output << " entry_mutation=" << backend_target_status.entry_mutations;
		output << " overflow=" << backend_target_status.route_overflows << '\n';
		output << "  backend_target_frame: state="
			<< engine_stereo_backend_target::to_string(backend_target_frame_status.state);
		output << " attempts=" << backend_target_frame_status.attempts;
		output << " complete=" << backend_target_frame_status.completions;
		output << " failures=" << backend_target_frame_status.failures;
		output << " selects=" << backend_target_frame_status.select_calls;
		output << " view_copies=" << backend_target_frame_status.view_copy_events;
		output << " unique=" << backend_target_frame_status.unique_targets;
		output << " invalid=" << backend_target_frame_status.invalid_observations;
		output << " apply_mismatch="
			<< backend_target_frame_status.application_mismatches;
		output << " entry_mutation=" << backend_target_frame_status.entry_mutations;
		output << " overflow=" << backend_target_frame_status.route_overflows;
		output << " generation_mismatch="
			<< backend_target_frame_status.device_generation_mismatches;
		output << " present=" << backend_target_frame_status.start_present_post_frame
			<< "->" << backend_target_frame_status.end_present_pre_frame;
		output << " present_post_thread="
			<< backend_target_frame_status.start_present_post_thread_id;
		output << " generation=" << backend_target_frame_status.device_generation;
		output << " boundary_thread=" << backend_target_frame_status.boundary_thread_id;
		output << " writers=" << backend_target_frame_status.active_writers;
		output << " max_writers=" << backend_target_frame_status.maximum_active_writers
			<< '\n';
	}
}
