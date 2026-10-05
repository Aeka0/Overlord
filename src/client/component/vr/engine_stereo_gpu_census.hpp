#pragma once
#include "component/vr/native_render_contract.hpp"
#include "engine_backend_probe.hpp"
#include "engine_stereo_constant_buffer_probe.hpp"
#include "engine_stereo_resource_ops.hpp"
#include <d3d11.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_gpu_census
{
	namespace request_control
	{
		[[nodiscard]] constexpr std::uint64_t make_token(
			const std::uint32_t sequence, const std::uint32_t delay_pairs) noexcept
		{
			return static_cast<std::uint64_t>(sequence) << 32 | delay_pairs;
		}

		[[nodiscard]] constexpr std::uint32_t sequence(
			const std::uint64_t token) noexcept
		{
			return static_cast<std::uint32_t>(token >> 32);
		}

		[[nodiscard]] constexpr std::uint32_t delay(
			const std::uint64_t token) noexcept
		{
			return static_cast<std::uint32_t>(token);
		}

		[[nodiscard]] constexpr bool launch_eligible(
			const std::uint64_t applied_token, const std::uint32_t remaining,
			const bool attempted) noexcept
		{
			return sequence(applied_token) != 0 && remaining == 0 && !attempted;
		}

		static_assert(!launch_eligible(0, 0, false));
		static_assert(launch_eligible(make_token(1, 0), 0, false));
		static_assert(!launch_eligible(make_token(1, 1), 1, false));
		static_assert(!launch_eligible(make_token(1, 0), 0, true));
	}

	enum class state : std::uint8_t { idle, pair_active, eye_active, complete, failed };
	enum class api : std::uint8_t { draw_indexed, draw, draw_indexed_instanced,
		draw_instanced, draw_auto, draw_indexed_instanced_indirect,
		draw_instanced_indirect, dispatch, dispatch_indirect,
		clear_render_target, clear_depth_stencil, copy_subresource_region,
		copy_resource, update_subresource, copy_structure_count, clear_uav_uint,
		clear_uav_float, generate_mips, resolve_subresource };
	enum class access_site : std::uint8_t
	{
		unknown, vs_srv, ps_srv, gs_srv, hs_srv, ds_srv, cs_srv, om_rtv, om_dsv,
		om_uav, cs_uav, clear_rtv, clear_dsv, copy_source, copy_destination,
		update_destination, structure_count_source, clear_uav, generate_mips_srv,
		resolve_source, resolve_destination
	};
	enum ordered_mismatch : std::uint32_t
	{
		mismatch_none = 0,
		mismatch_api = 1u << 0,
		mismatch_caller = 1u << 1,
		mismatch_arguments = 1u << 2,
		mismatch_program = 1u << 3,
		mismatch_output_target = 1u << 4,
		mismatch_render_target = 1u << 5,
		mismatch_depth_target = 1u << 6,
		mismatch_depth_state = 1u << 7,
		mismatch_blend_state = 1u << 8,
		mismatch_rasterizer_state = 1u << 9,
		mismatch_topology = 1u << 10,
		mismatch_index_buffer = 1u << 11,
		mismatch_constant_buffers = 1u << 12,
		mismatch_shader_resources = 1u << 13,
		mismatch_unordered_access = 1u << 14,
		mismatch_outputs = 1u << 15,
		mismatch_input_layout = 1u << 16,
		mismatch_blend_factor = 1u << 17,
		mismatch_viewport = 1u << 18,
		mismatch_scissor = 1u << 19,
	};
	inline constexpr std::size_t maximum_identities = 512;
	inline constexpr std::size_t maximum_bindings = 512;
	inline constexpr std::size_t maximum_depth_ranges = 32;
	inline constexpr std::size_t scanned_srv_slots = 32;
	inline constexpr std::size_t constant_buffer_slots =
		D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
	inline constexpr std::size_t maximum_observation_constant_buffers = 32;
	inline constexpr std::size_t maximum_observation_shader_resources = 64;
	inline constexpr std::uint64_t maximum_observations_per_eye = 8192;
	inline constexpr std::size_t maximum_category_mismatch_samples = 8;
	inline constexpr std::size_t maximum_shader_resource_mismatch_samples = 16;
	inline constexpr std::size_t dynamic_fx_family_count = 4;
	// A real owner pair has reached 387 GLASS draws. Retain enough headroom for
	// the complete family stream instead of turning valid right-eye effects into
	// an observation-cap false negative.
	inline constexpr std::size_t maximum_dynamic_fx_observations_per_family = 512;
	inline constexpr std::size_t maximum_dynamic_fx_mismatch_samples = 8;
	inline constexpr std::size_t dynamic_fx_stream_window_size = 3;
	// One mismatching invocation can expose all 28 VS+PS CB slots or all 32 PS
	// SRV slots without allowing its later samples to hide the pixel-stage cause.
	inline constexpr std::size_t maximum_dynamic_fx_binding_mismatch_samples = 64;
	// Retain enough byte and 32-bit word evidence to cover the compact view/code
	// globals used by dynamic FX.  The capture is one-shot and report-only; this
	// does not add work to the normal production path.
	inline constexpr std::size_t maximum_dynamic_fx_byte_difference_samples = 128;
	inline constexpr std::size_t maximum_dynamic_fx_word_difference_samples = 64;
	inline constexpr std::size_t maximum_dynamic_fx_reflected_variable_samples = 12;
	inline constexpr std::size_t dynamic_fx_arena_mesh_count = 8;
	inline constexpr std::size_t dynamic_fx_arena_raw_qword_count = 7;
	inline constexpr std::size_t maximum_dynamic_fx_arena_view_copies_per_output = 64;
	inline constexpr std::size_t maximum_dynamic_fx_arena_snapshots_per_output =
		maximum_dynamic_fx_arena_view_copies_per_output + 2;
	inline constexpr std::size_t dynamic_fx_vertex_binding_count = 4;
	inline constexpr std::size_t dynamic_fx_arena_transition_count = 6;
	inline constexpr std::size_t maximum_shader_debug_name = 96;
	inline constexpr std::size_t maximum_shader_binding_name = 64;
	inline constexpr std::size_t shader_stage_count = 6;
	enum class shader_stage : std::uint8_t
	{
		vs,
		ps,
		cs,
		gs,
		hs,
		ds,
	};
	enum class srv_shader_usage : std::uint8_t
	{
		unknown,
		unused,
		used,
	};
	struct identity_count { std::uintptr_t identity{}; std::uint64_t calls{}; };
	struct binding_count { std::uint8_t stage{}; std::uint8_t slot{};
		std::uintptr_t identity{}; std::uint64_t calls{}; };
	struct depth_range_count { std::uint32_t min_depth_bits{};
		std::uint32_t max_depth_bits{}; std::uint64_t viewports{}; };
	struct program_identity
	{
		std::uintptr_t vs{}, ps{}, cs{}, gs{}, hs{}, ds{};
	};
	struct resource_descriptor
	{
		bool captured{}, valid{};
		std::uint32_t dimension{}, width{}, height{}, depth{}, mip_levels{}, array_size{},
			format{}, sample_count{}, sample_quality{}, usage{}, bind_flags{},
			cpu_access_flags{}, misc_flags{}, byte_width{}, structure_byte_stride{};
	};
	struct access_observation
	{
		std::uint64_t call{};
		std::uint64_t output_binding_sequence{};
		access_site site{access_site::unknown};
		api operation{api::draw_indexed};
		std::uint8_t slot{};
		std::uintptr_t view_identity{}, caller{}, output_render_target_view{};
		std::uint32_t output_target_id{0xFFFFFFFFu};
		std::uint32_t resource_target_id{0xFFFFFFFFu};
		std::uint32_t view_format{}, view_dimension{};
		program_identity program{};
	};
	struct resource_access { std::uintptr_t identity{}; std::uint8_t read_mask{};
		std::uint8_t write_mask{}; bool right_first_read_before_write_candidate{};
		std::uint32_t target_entry_id{0xFFFFFFFFu};
		bool target_entry_captured{}, target_entry_stable{};
		native_render_contract::target_registry_entry target_entry_before{};
		native_render_contract::target_registry_entry target_entry_after{};
		resource_descriptor descriptor{};
		access_observation first_left_write{}, first_right_read{}, first_right_write{}; };
	struct vertex_buffer_binding
	{
		std::uintptr_t buffer{};
		std::uint32_t stride{}, offset{};
	};
	struct pipeline_snapshot
	{
		std::uintptr_t render_target_view{}, render_target_resource{};
		std::uintptr_t depth_stencil_view{}, depth_stencil_resource{};
		std::uintptr_t depth_stencil_state{}, blend_state{}, rasterizer_state{},
			input_layout{};
		std::uintptr_t index_buffer{};
		std::uint32_t stencil_reference{}, sample_mask{}, primitive_topology{},
			index_format{}, index_offset{}, viewport_count{}, scissor_count{};
		std::array<std::uint32_t, 4> blend_factor_bits{};
		std::uint64_t viewport_hash{}, scissor_hash{};
		std::array<vertex_buffer_binding, dynamic_fx_vertex_binding_count>
			vertex_buffers{};
		std::array<std::uint8_t, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>
			render_target_write_masks{};
		bool depth_reads{}, depth_writes{}, stencil_reads{}, stencil_writes{};
	};
	struct binding_signature
	{
		std::uint64_t constant_buffers{1469598103934665603ull};
		std::uint64_t shader_resources{1469598103934665603ull};
		std::uint64_t samplers{1469598103934665603ull};
		std::uint64_t unordered_access{1469598103934665603ull};
		std::uint64_t outputs{1469598103934665603ull};
		std::uint32_t constant_buffer_count{}, shader_resource_count{},
			sampler_count{}, unordered_access_count{}, output_count{};
	};
	struct srv_subresource_range
	{
		std::uint32_t format{}, dimension{};
		std::uint32_t most_detailed_mip{}, mip_levels{};
		std::uint32_t first_array_slice{}, array_size{};
		std::uint32_t first_element{}, element_count{};
	};
	struct srv_binding_identity
	{
		std::uintptr_t view{}, resource{};
		srv_subresource_range range{};
	};
	struct invocation_arguments
	{
		std::uint8_t count{};
		std::array<std::uint64_t, 6> values{};
	};
	enum class dynamic_fx_family : std::uint8_t
	{
		code_trans,
		glass,
		mark,
		spark,
		unknown = 0xFF,
	};
	enum class dynamic_fx_evidence : std::uint8_t
	{
		unfinalized,
		not_observed,
		invalid_shape,
		observed,
		truncated,
		incomplete,
	};
	enum class dynamic_fx_arena_phase : std::uint8_t
	{
		eye_begin,
		backend_view_copy,
		eye_end,
	};
	enum class dynamic_fx_arena_evidence : std::uint8_t
	{
		unfinalized,
		not_observed,
		complete,
		unreadable,
		truncated,
	};
	enum class dynamic_fx_arena_transition_kind : std::uint8_t
	{
		output0_begin_to_view,
		output0_view_to_end,
		output0_end_to_output1_begin,
		output1_begin_to_view,
		output1_view_to_end,
		output0_view_to_output1_view,
	};
	struct dynamic_fx_arena_mesh_snapshot
	{
		std::uintptr_t address{};
		bool readable{};
		std::array<std::uint64_t, dynamic_fx_arena_raw_qword_count> raw_qwords{};
	};
	struct dynamic_fx_arena_candidate_snapshot
	{
		bool applicable{}, pointer_slot_readable{}, complete{};
		std::uintptr_t pointer_slot{}, data_identity{};
		std::array<dynamic_fx_arena_mesh_snapshot,
			dynamic_fx_arena_mesh_count> meshes{};
	};
	struct dynamic_fx_arena_snapshot
	{
		bool present{};
		std::uint64_t sequence{}, view_copy_ordinal{};
		std::uint32_t output{};
		dynamic_fx_arena_phase phase{dynamic_fx_arena_phase::eye_begin};
		std::uintptr_t owner_record{}, backend_state{};
		bool data_identity_comparable{}, data_identity_equal{};
		dynamic_fx_arena_candidate_snapshot global{}, backend{};
	};
	struct dynamic_fx_arena_output_report
	{
		std::uint64_t snapshot_attempts{}, snapshot_completions{}, unreadable{},
			overflows{}, order_mismatches{}, view_copy_attempts{}, view_copy_stored{},
			linked_invocations{}, unlinked_invocations{};
		std::array<dynamic_fx_arena_snapshot,
			maximum_dynamic_fx_arena_snapshots_per_output> snapshots{};
		std::size_t snapshot_count{};
	};
	struct dynamic_fx_arena_transition
	{
		dynamic_fx_arena_transition_kind kind{
			dynamic_fx_arena_transition_kind::output0_begin_to_view};
		bool observed{}, global_identity_comparable{}, global_identity_equal{},
			backend_identity_comparable{}, backend_identity_equal{},
			global_raw_comparable{}, backend_raw_comparable{};
		std::uint64_t from_sequence{}, to_sequence{};
		std::array<std::uint8_t, dynamic_fx_arena_mesh_count>
			global_changed_qword_masks{}, backend_changed_qword_masks{};
	};
	struct dynamic_fx_arena_report
	{
		bool finalized{};
		dynamic_fx_arena_evidence evidence{
			dynamic_fx_arena_evidence::unfinalized};
		std::uint64_t snapshot_attempts{}, snapshot_completions{}, unreadable{},
			overflows{}, order_mismatches{}, foreign_pair{}, foreign_output{},
			foreign_thread{}, data_identity_comparisons{}, data_identity_mismatches{},
			cross_output_global_identity_comparisons{},
			cross_output_global_identity_mismatches{},
			cross_output_backend_identity_comparisons{},
			cross_output_backend_identity_mismatches{},
			linked_invocations{}, unlinked_invocations{};
		std::uint64_t next_sequence{};
		std::array<dynamic_fx_arena_output_report, 2> outputs{};
		std::array<dynamic_fx_arena_transition,
			dynamic_fx_arena_transition_count> transitions{};
	};
	struct dynamic_fx_invocation
	{
		std::uint64_t output_ordinal{}, family_ordinal{};
		std::uint64_t arena_view_copy_snapshot_sequence{};
		api operation{api::draw_indexed};
		std::uintptr_t caller{};
		invocation_arguments arguments{};
		program_identity program{};
		std::uint32_t output_target_id{0xFFFFFFFFu};
		std::uintptr_t input_layout{}, depth_stencil_state{}, blend_state{},
			rasterizer_state{};
		std::uint32_t stencil_reference{}, sample_mask{}, primitive_topology{},
			viewport_count{}, scissor_count{};
		std::array<std::uint32_t, 4> blend_factor_bits{};
		std::uint64_t viewport_hash{}, scissor_hash{};
		std::uintptr_t index_buffer{};
		std::uint32_t index_format{}, index_offset{};
		// DrawIndexed determines an exact byte interval in the bound index buffer
		// without touching GPU memory. A later asynchronous evidence probe can use
		// this interval to stage/hash the indices, then derive the referenced VB
		// intervals from the existing bindings below. The census itself never
		// performs a readback or changes production rendering/timing.
		struct index_buffer_range
		{
			bool exact{}, hash_eligible{};
			std::uint64_t byte_offset{}, byte_count{};
		} index_range{};
		std::array<vertex_buffer_binding, dynamic_fx_vertex_binding_count>
			vertex_buffers{};
		binding_signature bindings{};
	};
	struct dynamic_fx_eye_family_report
	{
		std::uint64_t range_hits{}, calls{}, invalid_invocations{},
			zero_index_counts{}, observation_overflows{};
		std::array<dynamic_fx_invocation,
			maximum_dynamic_fx_observations_per_family> observations{};
		std::size_t observation_count{};
	};
	struct dynamic_fx_eye_report
	{
		std::uint64_t range_hits{}, semantic_hits{}, invalid_invocations{};
		std::array<dynamic_fx_eye_family_report, dynamic_fx_family_count> families{};
	};
	struct dynamic_fx_mismatch_sample
	{
		dynamic_fx_family family{dynamic_fx_family::unknown};
		std::uint64_t family_ordinal{};
		bool output0_present{}, output1_present{};
		dynamic_fx_invocation output0{}, output1{};
	};
	struct dynamic_fx_stream_entry
	{
		bool present{}, semantic{};
		dynamic_fx_family family{dynamic_fx_family::unknown};
		dynamic_fx_invocation invocation{};
	};
	struct dynamic_fx_stream_ordinal
	{
		std::uint64_t output_ordinal{};
		std::array<dynamic_fx_stream_entry, 2> outputs{};
	};
	struct dynamic_fx_stream_window
	{
		bool captured{};
		std::uint64_t event_output_ordinal{};
		// [0/1/2] are the immediately preceding, event, and immediately
		// following global output ordinals. Missing entries remain explicit.
		std::array<dynamic_fx_stream_ordinal, dynamic_fx_stream_window_size>
			ordinals{};
	};
	struct dynamic_fx_stream_comparison
	{
		bool finalized{}, complete{};
		std::uint64_t output0_observations{}, output1_observations{},
			compared_ordinals{}, dynamic_ordinals{}, matched_family_invocations{},
			family_transitions{}, semantic_mismatches{}, missing_output0{},
			missing_output1{}, binding_comparisons{};
		dynamic_fx_stream_window first_family_transition{},
			first_semantic_mismatch{}, first_missing_output0{},
			first_missing_output1{};
	};
	struct dynamic_fx_constant_buffer_binding
	{
		std::uint8_t stage{}, slot{};
		std::uintptr_t identity{};
		engine_stereo_constant_buffer_probe::content_snapshot content{};
	};
	struct dynamic_fx_reflected_variable_difference
	{
		std::uint64_t name_hash{};
		std::uint32_t start_offset{}, size{}, differing_bytes{},
			first_difference{}, last_difference{};
		std::array<char, maximum_shader_binding_name> name{};
	};
	struct dynamic_fx_constant_buffer_reflection
	{
		std::uintptr_t shader{};
		bool attempted{}, available{}, binding_declared{}, complete{};
		std::uint64_t shader_debug_name_hash{}, buffer_name_hash{};
		std::uint32_t buffer_size{}, mapped_differing_bytes{},
			unmapped_differing_bytes{};
		std::array<char, maximum_shader_debug_name> shader_debug_name{};
		std::array<char, maximum_shader_binding_name> buffer_name{};
		std::array<dynamic_fx_reflected_variable_difference,
			maximum_dynamic_fx_reflected_variable_samples> variables{};
		std::size_t variable_count{};
		std::uint64_t variable_overflows{};
	};
	struct dynamic_fx_shader_resource_binding
	{
		std::uint8_t slot{};
		srv_binding_identity binding{};
	};
	struct sampler_descriptor
	{
		bool captured{};
		std::uint32_t filter{}, address_u{}, address_v{}, address_w{},
			mip_lod_bias_bits{}, maximum_anisotropy{}, comparison_function{},
			minimum_lod_bits{}, maximum_lod_bits{};
		std::array<std::uint32_t, 4> border_color_bits{};
	};
	struct dynamic_fx_sampler_binding
	{
		std::uint8_t slot{};
		std::uintptr_t identity{};
		sampler_descriptor descriptor{};
	};
	struct dynamic_fx_constant_buffer_mismatch_sample
	{
		dynamic_fx_family family{dynamic_fx_family::unknown};
		std::uint64_t output_ordinal{}, family_ordinal{},
			output0_family_ordinal{}, output1_family_ordinal{},
			output0_output_ordinal{}, output1_output_ordinal{};
		std::uint8_t stage{}, slot{};
		bool output0_present{}, output1_present{};
		bool identity_mismatch{}, content_unknown{}, content_mismatch{};
		bool byte_comparison_available{}, byte_comparison_complete{};
		std::uint32_t byte_compared{}, byte_difference_count{},
			first_byte_difference{}, last_byte_difference{};
		std::array<std::uint16_t, maximum_dynamic_fx_byte_difference_samples>
			byte_difference_offsets{};
		std::array<std::uint8_t, maximum_dynamic_fx_byte_difference_samples>
			output0_difference_values{}, output1_difference_values{};
		std::size_t byte_difference_sample_count{};
		std::uint32_t word_compared{}, word_difference_count{},
			first_word_difference{}, last_word_difference{};
		std::array<std::uint16_t, maximum_dynamic_fx_word_difference_samples>
			word_difference_offsets{};
		std::array<std::uint32_t, maximum_dynamic_fx_word_difference_samples>
			output0_word_values{}, output1_word_values{};
		std::size_t word_difference_sample_count{};
		std::uint64_t occurrences{1}, last_output_ordinal{};
		program_identity output0_program{}, output1_program{};
		dynamic_fx_constant_buffer_binding output0{}, output1{};
		dynamic_fx_constant_buffer_reflection output0_reflection{},
			output1_reflection{};
	};
	struct dynamic_fx_shader_resource_mismatch_sample
	{
		dynamic_fx_family family{dynamic_fx_family::unknown};
		std::uint64_t output_ordinal{}, family_ordinal{},
			output0_family_ordinal{}, output1_family_ordinal{};
		std::uint8_t slot{};
		bool output0_present{}, output1_present{};
		dynamic_fx_shader_resource_binding output0{}, output1{};
	};
	struct dynamic_fx_sampler_mismatch_sample
	{
		dynamic_fx_family family{dynamic_fx_family::unknown};
		std::uint64_t output_ordinal{}, family_ordinal{},
			output0_family_ordinal{}, output1_family_ordinal{};
		std::uint8_t slot{};
		bool output0_present{}, output1_present{}, identity_mismatch{},
			descriptor_mismatch{};
		dynamic_fx_sampler_binding output0{}, output1{};
	};
	struct dynamic_fx_family_comparison
	{
		std::uint64_t output0_calls{}, output1_calls{}, paired_calls{}, compared_calls{},
			global_ordinal_matches{},
			missing_output0{}, missing_output1{}, output0_zero_index_counts{},
			output1_zero_index_counts{}, index_count_mismatches{},
			base_vertex_mismatches{}, ordinal_mismatches{}, program_mismatches{},
			caller_mismatches{}, output_target_mismatches{}, index_buffer_mismatches{},
			vertex_buffer_mismatches{}, vertex_stride_mismatches{},
			vertex_offset_mismatches{}, argument_shape_mismatches{},
			input_layout_mismatches{}, depth_state_mismatches{},
			blend_state_mismatches{}, blend_factor_mismatches{},
			rasterizer_state_mismatches{}, topology_mismatches{},
			viewport_mismatches{}, scissor_mismatches{},
			constant_buffer_signature_mismatches{},
			shader_resource_signature_mismatches{}, sampler_signature_mismatches{},
			constant_buffer_slot_comparisons{},
			constant_buffer_identity_mismatches{},
			constant_buffer_content_comparisons{},
			constant_buffer_content_mismatches{},
			constant_buffer_content_unknown{}, ps_shader_resource_comparisons{},
			ps_shader_resource_mismatches{}, ps_sampler_comparisons{},
			ps_sampler_identity_mismatches{}, ps_sampler_descriptor_mismatches{},
			binding_detail_drops{},
			start_index_delta_eligible{},
			start_index_delta_ineligible{}, nonzero_start_index_deltas{};
		std::array<std::uint64_t, dynamic_fx_vertex_binding_count>
			vertex_buffer_mismatches_by_slot{}, vertex_stride_mismatches_by_slot{},
			vertex_offset_mismatches_by_slot{};
		bool comparison_finalized{}, evidence_available{}, comparison_complete{};
		// All delta fields are signed output1.StartIndex-output0.StartIndex.
		bool start_index_delta_observed{}, start_index_delta_constant{};
		std::int64_t start_index_delta_first{}, start_index_delta_min{},
			start_index_delta_max{};
		std::array<dynamic_fx_mismatch_sample,
			maximum_dynamic_fx_mismatch_samples> mismatch_samples{};
		std::size_t mismatch_sample_count{};
		std::uint64_t mismatch_sample_overflows{};
		std::array<dynamic_fx_constant_buffer_mismatch_sample,
			maximum_dynamic_fx_binding_mismatch_samples> constant_buffer_samples{};
		std::size_t constant_buffer_sample_count{};
		std::uint64_t constant_buffer_sample_overflows{};
		std::array<dynamic_fx_shader_resource_mismatch_sample,
			maximum_dynamic_fx_binding_mismatch_samples> ps_shader_resource_samples{};
		std::size_t ps_shader_resource_sample_count{};
		std::uint64_t ps_shader_resource_sample_overflows{};
		std::array<dynamic_fx_sampler_mismatch_sample,
			maximum_dynamic_fx_binding_mismatch_samples> ps_sampler_samples{};
		std::size_t ps_sampler_sample_count{};
		std::uint64_t ps_sampler_sample_overflows{};
	};
	struct dynamic_fx_report
	{
		std::uint64_t range_hits{}, semantic_hits{}, invalid_invocations{},
			observation_overflows{}, content_byte_capture_attempts{},
			content_byte_capture_completions{}, content_byte_capture_unavailable{},
			content_byte_capture_overflows{}, reflection_attempts{},
			reflection_completions{}, reflection_bytecode_missing{},
			reflection_bytecode_oversized{}, reflection_failures{},
			reflection_stage_mismatches{}, reflection_buffer_overflows{},
			reflection_variable_overflows{}, reflection_name_truncations{};
		// eyes[0/1] are first/second owner replay and output-eye order. They are not
		// view_eye and are deliberately unaffected by vr_engineSwapEyes.
		bool comparison_finalized{}, evidence_available{}, comparison_complete{};
		dynamic_fx_evidence evidence{dynamic_fx_evidence::unfinalized};
		std::array<dynamic_fx_eye_report, 2> eyes{};
		std::array<dynamic_fx_family_comparison, dynamic_fx_family_count>
			families{};
		dynamic_fx_stream_comparison stream{};
		dynamic_fx_arena_report arena{};
	};
	struct observation_signature
	{
		api operation{api::draw_indexed};
		std::uintptr_t caller{};
		std::uint64_t argument_hash{};
		invocation_arguments arguments{};
		program_identity program{};
		std::uint32_t output_target_id{0xFFFFFFFFu};
		pipeline_snapshot pipeline{};
		binding_signature bindings{};
	};
	struct observation_context
	{
		api operation{api::draw_indexed};
		std::uintptr_t caller{};
		std::uint32_t output_target_id{0xFFFFFFFFu};
	};
	struct argument_mismatch_sample
	{
		std::uint64_t ordinal{};
		observation_signature left{}, right{};
	};
	struct constant_buffer_mismatch_sample
	{
		std::uint64_t ordinal{};
		observation_context left{}, right{};
		std::uint8_t stage{}, slot{};
		std::uintptr_t left_identity{}, right_identity{};
	};
	struct shader_resource_mismatch_sample
	{
		std::uint64_t ordinal{};
		observation_context left{}, right{};
		invocation_arguments left_arguments{}, right_arguments{};
		std::uint8_t stage{}, slot{};
		srv_binding_identity left_binding{}, right_binding{};
		struct shader_binding_classification
		{
			srv_shader_usage usage{srv_shader_usage::unknown};
			std::uintptr_t shader{};
			std::uint64_t shader_debug_name_hash{}, binding_name_hash{};
			std::uint32_t input_type{}, return_type{}, dimension{}, bind_point{},
				bind_count{};
			std::array<char, maximum_shader_debug_name> shader_debug_name{};
			std::array<char, maximum_shader_binding_name> binding_name{};
		};
		shader_binding_classification left_shader{}, right_shader{};
		resource_descriptor left_resource{}, right_resource{};
	};
	struct srv_usage_slot_summary
	{
		std::uint64_t mismatches{}, used{}, unused{}, unknown{};
		std::uint64_t left_used{}, left_unused{}, left_unknown{};
		std::uint64_t right_used{}, right_unused{}, right_unknown{};
		std::uint64_t descriptor_same{}, descriptor_different{},
			descriptor_unknown{};
	};
	struct srv_usage_classification
	{
		bool classification_complete{true};
		std::uint64_t slot_mismatches{}, used{}, unused{}, unknown{};
		std::uint64_t descriptor_same{}, descriptor_different{},
			descriptor_unknown{};
		std::uint64_t reflected_shaders{}, reflected_bindings{};
		std::uint64_t shader_cache_overflows{}, descriptor_cache_overflows{};
		std::uint64_t shader_bytecode_missing{}, shader_bytecode_oversized{},
			shader_reflection_failures{}, shader_stage_mismatches{},
			shader_binding_overflows{}, sample_overflows{};
		std::uint64_t shader_debug_name_missing{}, shader_debug_name_truncations{},
			binding_name_truncations{}, shader_cache_misses{},
			descriptor_cache_misses{};
		std::array<std::array<srv_usage_slot_summary, scanned_srv_slots>,
			shader_stage_count> slots{};
	};
	struct ordered_comparison
	{
		std::uint64_t comparisons{}, mismatches{}, api_mismatches{}, caller_mismatches{},
			argument_mismatches{}, program_mismatches{}, output_target_mismatches{},
			render_target_mismatches{}, depth_target_mismatches{},
			depth_state_mismatches{}, blend_state_mismatches{},
			input_layout_mismatches{}, blend_factor_mismatches{},
			viewport_mismatches{}, scissor_mismatches{},
			rasterizer_state_mismatches{}, topology_mismatches{},
			index_buffer_mismatches{}, constant_buffer_mismatches{},
			shader_resource_mismatches{}, unordered_access_mismatches{},
			output_binding_mismatches{}, constant_buffer_snapshot_truncations{},
			shader_resource_snapshot_truncations{}, snapshot_static_storage_bytes{},
			constant_buffer_bindings_dropped{}, shader_resource_bindings_dropped{};
		std::uint64_t first_mismatch_ordinal{};
		std::uint32_t first_mismatch_mask{};
		observation_signature first_left{}, first_right{};
		std::array<argument_mismatch_sample,
			maximum_category_mismatch_samples> argument_samples{};
		std::array<constant_buffer_mismatch_sample,
			maximum_category_mismatch_samples> constant_buffer_samples{};
		std::array<shader_resource_mismatch_sample,
			maximum_shader_resource_mismatch_samples> shader_resource_samples{};
		std::size_t argument_sample_count{}, constant_buffer_sample_count{},
			shader_resource_sample_count{};
		srv_usage_classification shader_resource_usage{};
	};
	struct eye_report
	{
		std::uint64_t draw_calls{}, dispatch_calls{}, observations{},
			clear_render_target_calls{}, clear_depth_stencil_calls{},
			clear_depth_calls{}, clear_stencil_calls{};
		std::array<identity_count, maximum_identities> vs_shaders{}, ps_shaders{},
			cs_shaders{}, depth_states{}, blend_states{}, rasterizer_states{};
		std::size_t vs_shader_count{}, ps_shader_count{}, cs_shader_count{};
		std::size_t depth_state_count{}, blend_state_count{}, rasterizer_state_count{};
		std::array<binding_count, maximum_bindings> constant_buffers{};
		std::size_t constant_buffer_count{};
		std::array<depth_range_count, maximum_depth_ranges> depth_ranges{};
		std::size_t depth_range_count{};
		std::array<std::uint64_t, engine_stereo_resource_ops::api_count>
			resource_operations{};
		std::array<std::uint64_t, shader_stage_count> srv_bindings_seen{},
			srv_bindings_admitted{}, srv_bindings_ignored{};
		std::uint64_t om_rtv_bindings_seen{}, om_rtv_bindings_admitted{},
			om_rtv_bindings_ignored{}, om_dsv_bindings_seen{},
			om_dsv_bindings_admitted{}, om_dsv_bindings_ignored{},
			om_uav_bindings_seen{}, om_uav_bindings_admitted{},
			om_uav_bindings_ignored{}, cs_uav_bindings_seen{},
			cs_uav_bindings_admitted{}, cs_uav_bindings_ignored{};
	};
	struct report
	{
		state current_state{state::idle}; std::uint64_t pair_id{};
		std::uint8_t completed_eye_mask{}; std::uint64_t query_failures{}, query_calls{},
			overflows{}, identity_overflows{}, binding_overflows{}, resource_overflows{},
			depth_overflows{}, observation_overflows{}, hazard_candidates{},
			foreign_thread_observations{},
			foreign_context_observations{};
		std::uint64_t rejected_dispatch_graphics_srv{}, rejected_dispatch_om{},
			rejected_draw_cs{}, rejected_null_shader{}, rejected_no_color_write{};
		std::uintptr_t context_identity{};
		std::uint32_t owner_thread_id{};
		std::uint32_t srv_slots_scanned{static_cast<std::uint32_t>(scanned_srv_slots)};
		std::array<eye_report, 2> eyes{};
		std::array<resource_access, maximum_identities> resources{};
		std::size_t resource_count{};
		ordered_comparison ordered{};
		dynamic_fx_report dynamic_fx{};
		engine_stereo_resource_ops::status resource_operation_hooks{};
		engine_stereo_constant_buffer_probe::report constant_buffer_probe{};
	};
	// The caller owns context for the complete pair lifetime. The census records only
	// draw/dispatch hooks reached on this exact context and the begin_pair thread.
	[[nodiscard]] bool begin_pair(std::uint64_t pair_id,
		ID3D11DeviceContext* context) noexcept;
	[[nodiscard]] bool begin_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	[[nodiscard]] bool end_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	[[nodiscard]] bool end_pair(std::uint64_t pair_id) noexcept;
	void observe(ID3D11DeviceContext* context, api operation, std::uintptr_t caller,
		std::uint32_t output_target_id, std::uintptr_t output_render_target_view,
		std::uint64_t output_binding_sequence,
		const invocation_arguments& arguments) noexcept;
	void observe_clear_render_target(ID3D11DeviceContext* context,
		ID3D11RenderTargetView* view, std::uintptr_t caller,
		std::uint32_t output_target_id, std::uintptr_t output_render_target_view,
		std::uint64_t output_binding_sequence) noexcept;
	void observe_clear_depth_stencil(ID3D11DeviceContext* context,
		ID3D11DepthStencilView* view, std::uint32_t clear_flags,
		std::uintptr_t caller, std::uint32_t output_target_id,
		std::uintptr_t output_render_target_view,
		std::uint64_t output_binding_sequence) noexcept;
	[[nodiscard]] dynamic_fx_family classify_dynamic_fx_caller(
		std::uintptr_t caller) noexcept;
	// Production uses the fixed H2 global pointer slot. The test-only entry keeps
	// the exact lifecycle/ordering checks while supplying committed fake memory;
	// it is not a mutable production address switch.
	void note_dynamic_fx_arena_boundary(std::uint64_t pair_id,
		std::uint32_t output, dynamic_fx_arena_phase phase,
		std::uintptr_t owner_record, void* backend_state = nullptr) noexcept;
	void note_dynamic_fx_arena_boundary_for_test(std::uint64_t pair_id,
		std::uint32_t output, dynamic_fx_arena_phase phase,
		std::uintptr_t owner_record, std::uintptr_t global_pointer_slot,
		void* backend_state = nullptr) noexcept;
	[[nodiscard]] state get_status() noexcept;
	void get_report(report& output) noexcept;
	// Control-plane only. Refuses to reset an active pair/eye transaction.
	[[nodiscard]] bool reset() noexcept;
	// Device/shutdown cancellation. Terminates any active one-shot observer and
	// always closes the constant-buffer history hot-path gate.
	void cancel() noexcept;
}
