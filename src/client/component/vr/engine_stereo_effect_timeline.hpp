#pragma once

#include "engine_stereo_execution.hpp"
#include "engine_stereo_resource_ops.hpp"

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_effect_timeline
{
	inline constexpr std::size_t maximum_pairs = 256;
	inline constexpr std::size_t dynamic_family_count = 4;
	inline constexpr std::size_t ssr_srv_slots = 14;
	inline constexpr std::size_t ssr_constant_buffer_slots = 5;
	inline constexpr std::size_t sampled_dynamic_srv_slots = 16;
	inline constexpr std::size_t sampled_dynamic_sampler_slots = 16;
	inline constexpr std::size_t sampled_dynamic_constant_buffer_slots = 14;
	inline constexpr std::size_t maximum_watched_resources = 32;
	inline constexpr std::size_t compared_stream_count = 5;
	inline constexpr std::size_t maximum_compared_invocations = 1024;
	inline constexpr std::size_t divergence_window_size = 5;
	inline constexpr std::size_t tracked_target_count = 3;
	inline constexpr std::size_t maximum_target_view_samples = 16;
	inline constexpr std::size_t maximum_clear_events = 128;
	inline constexpr std::array<std::uint32_t, tracked_target_count>
		tracked_target_ids{4u, 80u, 91u};

	enum class compared_stream : std::uint8_t
	{
		ssr,
		code_trans,
		glass,
		mark,
		spark,
	};

	enum class stream_alignment : std::uint8_t
	{
		unclassified,
		extra_output0,
		extra_output1,
		replacement,
		tail_count,
	};

	enum class state : std::uint8_t
	{
		idle,
		arm_pending,
		capturing,
		mark_pending,
		frozen,
		failed,
	};

	struct record_snapshot
	{
		bool valid{};
		std::uintptr_t record{};
		std::uint64_t current_view_hash{};
		std::uint64_t history_view_hash{};
		std::uint64_t ssr_source_hash{};
		std::array<std::uint32_t, 4> eye_offset_bits{};
		std::array<std::uint32_t, 4> ssr_previous_eye_bits{};
		std::array<std::uint32_t, 4> ssr_parameter_bits{};
		std::array<std::uint32_t, 4> ssr_clip_lookup_bits{};
		std::array<std::uint32_t, 4> ssr_clip_to_fade_bits{};
	};

	struct invocation_fingerprint
	{
		bool present{};
		std::uint8_t operation{}, argument_count{};
		std::uintptr_t caller{};
		std::uint32_t output_target{0xFFFFFFFFu};
		std::array<std::uint64_t, 6> arguments{};
		std::uintptr_t pixel_shader{}, blend_state{};
		std::uint32_t sample_mask{};
		std::array<std::uint32_t, 4> blend_factor_bits{};
		std::uint64_t shader_resource_identity_hash{};
		std::uint64_t shader_resource_descriptor_hash{};
		std::uint64_t constant_buffer_identity_hash{};
		std::uint64_t constant_buffer_content_hash{};
		std::uint16_t constant_buffer_known_mask{};
		std::uint16_t constant_buffer_unknown_mask{};
	};

	struct shader_resource_binding_sample
	{
		std::uintptr_t view{};
		std::uintptr_t resource{};
		std::uint64_t descriptor_hash{};
	};

	struct constant_buffer_binding_sample
	{
		std::uintptr_t buffer{};
		std::uint64_t hash_low{};
		std::uint64_t hash_high{};
		std::uint32_t byte_width{};
		bool known{};
	};

	struct shader_resource_slot_difference
	{
		std::uint32_t comparisons{};
		std::uint32_t view_identity_mismatches{};
		std::uint32_t resource_identity_mismatches{};
		std::uint32_t descriptor_mismatches{};
		std::uint64_t first_output0_ordinal{};
		std::uint64_t first_output1_ordinal{};
		shader_resource_binding_sample output0{}, output1{};
	};

	struct constant_buffer_slot_difference
	{
		std::uint32_t comparisons{};
		std::uint32_t identity_mismatches{};
		std::uint32_t known_state_mismatches{};
		std::uint32_t content_comparisons{};
		std::uint32_t content_mismatches{};
		std::uint64_t first_output0_ordinal{};
		std::uint64_t first_output1_ordinal{};
		constant_buffer_binding_sample output0{}, output1{};
	};

	struct pipeline_semantic_comparison
	{
		bool observed{};
		std::uint32_t aligned_invocations{};
		std::uint32_t structural_resyncs{};
		std::uint32_t pixel_shader_mismatches{};
		std::uint32_t blend_state_mismatches{};
		std::uint32_t sample_mask_mismatches{};
		std::uint32_t blend_factor_mismatches{};
		std::uint64_t first_output0_ordinal{};
		std::uint64_t first_output1_ordinal{};
		invocation_fingerprint first_output0{}, first_output1{};
		std::array<shader_resource_slot_difference,
			sampled_dynamic_srv_slots> shader_resources{};
		std::array<constant_buffer_slot_difference,
			sampled_dynamic_constant_buffer_slots> constant_buffers{};
	};

	struct stream_divergence
	{
		bool observed{}, output0_overflow{}, output1_overflow{};
		compared_stream stream{compared_stream::ssr};
		stream_alignment alignment{stream_alignment::unclassified};
		std::uint8_t output0_skip{}, output1_skip{};
		std::uint64_t first_ordinal{}, window_first_ordinal{};
		std::uint32_t output0_count{}, output1_count{};
		std::array<invocation_fingerprint, divergence_window_size> output0{},
			output1{};
		pipeline_semantic_comparison semantics{};
	};

	struct constant_buffer_sample
	{
		std::uintptr_t buffer{};
		std::uintptr_t upload_caller{};
		std::uint64_t upload_generation{};
		std::uint64_t hash_low{};
		std::uint64_t hash_high{};
		std::uint32_t byte_width{};
		std::uint8_t source{};
		bool known{};
	};

	struct resource_writer_sample
	{
		bool known{};
		std::uint64_t sequence{};
		std::uint64_t pair_id{};
		std::uint32_t eye{2};
		std::uint32_t destination_subresource{};
		std::uint8_t operation{};
		std::uintptr_t caller{};
		std::uintptr_t source{};
	};

	struct ssr_srv_sample
	{
		std::uintptr_t view{};
		std::uintptr_t resource{};
		resource_writer_sample last_writer{};
	};

	struct draw_fingerprint
	{
		bool captured{};
		std::uintptr_t caller{};
		std::uintptr_t pixel_shader{};
		std::uintptr_t blend_state{};
		std::uint32_t sample_mask{};
		std::array<std::uint32_t, 4> blend_factor_bits{};
		std::uint64_t shader_resource_hash{};
		std::uint64_t sampler_hash{};
		std::uint64_t constant_buffer_hash{};
		std::uint32_t constant_buffers_known{};
		std::uint32_t constant_buffers_unknown{};
		record_snapshot source{};
	};

	struct dynamic_family_sample
	{
		std::uint64_t calls{};
		std::uint64_t stream_hash{};
		std::uint64_t pipeline_hash{};
		std::uint32_t detailed_samples{};
		draw_fingerprint first{};
		draw_fingerprint last{};
	};

	struct ssr_consumer_sample
	{
		std::uint64_t calls{};
		std::uintptr_t pixel_shader{};
		std::uintptr_t blend_state{};
		std::uint32_t sample_mask{};
		std::array<std::uint32_t, 4> blend_factor_bits{};
		std::array<ssr_srv_sample, ssr_srv_slots> shader_resources{};
		std::array<constant_buffer_sample, ssr_constant_buffer_slots>
			constant_buffers{};
		std::uint64_t shader_resource_hash{};
		std::uint64_t constant_buffer_hash{};
		std::uint32_t writer_known{};
		std::uint32_t writer_unknown{};
		record_snapshot source{};
	};

	struct target_view_sample
	{
		std::uintptr_t view{};
		std::uintptr_t resource{};
	};

	struct target_lifecycle_sample
	{
		std::uint32_t target_id{0xFFFFFFFFu};
		std::uint64_t invocation_calls{};
		std::array<std::uint64_t, engine_stereo_execution::api_count>
			per_api{};
		std::uint64_t first_invocation_event{};
		std::uint64_t last_invocation_event{};
		std::uintptr_t first_output_view{};
		std::uintptr_t first_output_resource{};
		std::uintptr_t last_output_view{};
		std::uintptr_t last_output_resource{};
		std::uint32_t output_view_count{};
		std::uint32_t output_view_overflow{};
		std::uint32_t output_resource_query_failures{};
		std::array<target_view_sample, maximum_target_view_samples> output_views{};
		std::uint64_t binding_clear_calls{};
		std::uint64_t resource_clear_calls{};
		std::uint64_t exact_clear_calls{};
		std::uint64_t mismatched_clear_calls{};
		std::uint64_t first_clear_event{};
		std::uint64_t last_clear_event{};
		std::uintptr_t first_clear_view{};
		std::uintptr_t first_clear_resource{};
		std::uintptr_t last_clear_view{};
		std::uintptr_t last_clear_resource{};
	};

	struct clear_event_sample
	{
		std::uint64_t event_sequence{};
		std::uintptr_t caller{};
		std::uint32_t binding_target{0xFFFFFFFFu};
		std::uintptr_t clear_view{};
		std::uintptr_t clear_resource{};
		std::uintptr_t output_view{};
		std::uintptr_t output_resource{};
	};

	struct eye_sample
	{
		bool began{};
		bool ended{};
		std::uint64_t invocations{};
		std::uint64_t gpu_events{};
		std::uint32_t clear_event_count{};
		std::uint32_t clear_event_overflow{};
		record_snapshot begin_record{};
		record_snapshot end_record{};
		ssr_consumer_sample ssr{};
		std::array<dynamic_family_sample, dynamic_family_count> dynamic{};
		std::array<target_lifecycle_sample, tracked_target_count> targets{};
		std::array<clear_event_sample, maximum_clear_events> clear_events{};
	};

	struct pair_sample
	{
		std::uint64_t capture_sequence{};
		std::uint64_t pair_id{};
		std::uint64_t started_tick{};
		std::uint64_t completed_tick{};
		std::uint64_t device_generation{};
		std::uint32_t owner_thread{};
		std::uint8_t completed_eye_mask{};
		bool completed{};
		std::array<eye_sample, 2> eyes{};
		std::array<stream_divergence, compared_stream_count> divergences{};
	};

	struct report
	{
		state current{state::idle};
		bool installed{};
		bool observers_attached{};
		bool history_tracking_active{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint64_t arm_requests{};
		std::uint64_t arm_applications{};
		std::uint64_t mark_requests{};
		std::uint64_t mark_applications{};
		std::uint64_t pairs_started{};
		std::uint64_t pairs_completed{};
		std::uint64_t pairs_incomplete{};
		std::uint64_t pairs_overwritten{};
		std::uint64_t invocation_callbacks{};
		std::uint64_t resource_callbacks{};
		std::uint64_t clear_callbacks{};
		std::uint64_t foreign_thread_callbacks{};
		std::uint64_t foreign_context_callbacks{};
		std::uint64_t watched_resource_count{};
		std::uint64_t watched_resource_overflows{};
		std::uint64_t watched_resource_writes{};
		std::uint64_t compared_stream_overflows{};
		std::uint64_t capture_first_pair{};
		std::uint64_t capture_last_pair{};
		std::uint64_t frozen_pair{};
		std::uint32_t sample_count{};
		std::array<pair_sample, maximum_pairs> samples{};
	};

	// The D3D observers remain installed but are O(1) no-ops outside an explicit
	// capture. Arm/reset and mark/freeze are consumed only at owner boundaries.
	[[nodiscard]] bool install(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	[[nodiscard]] bool request_arm() noexcept;
	[[nodiscard]] bool request_mark() noexcept;

	[[nodiscard]] bool begin_pair(std::uint64_t pair_id, ID3D11DeviceContext* context,
		std::uint64_t device_generation, std::uint32_t owner_thread,
		const void* left_record, const void* right_record) noexcept;
	[[nodiscard]] bool begin_eye(std::uint64_t pair_id, std::uint32_t eye,
		const void* record) noexcept;
	void end_eye(std::uint64_t pair_id, std::uint32_t eye,
		const void* record) noexcept;
	void end_pair(std::uint64_t pair_id, bool completed) noexcept;

	void observe_invocation(ID3D11DeviceContext* context,
		engine_stereo_execution::api operation, std::uintptr_t caller,
		std::uint32_t output_target, std::uintptr_t output_rtv,
		std::uint64_t binding_sequence, std::uint8_t argument_count,
		const std::array<std::uint64_t, 6>& arguments) noexcept;
	void observe_resource_operation(
		const engine_stereo_resource_ops::event& event) noexcept;
	void observe_clear_render_target(ID3D11DeviceContext* context,
		ID3D11RenderTargetView* view, std::uintptr_t caller,
		std::uint32_t output_target, std::uintptr_t output_rtv,
		std::uint64_t binding_sequence) noexcept;

	// Full samples are copied only after mark has frozen the producer-owned ring.
	// While capture is active this returns just the atomic lifecycle counters.
	void get_report(report& output) noexcept;
	[[nodiscard]] const char* to_string(state value) noexcept;
}
