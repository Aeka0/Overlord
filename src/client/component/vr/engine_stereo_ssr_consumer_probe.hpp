#pragma once

#include "engine_stereo_constant_buffer_probe.hpp"
#include "engine_stereo_dxbc_declarations.hpp"
#include "engine_stereo_ssr_consumer_window.hpp"

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_ssr_consumer_probe
{
	inline constexpr std::uintptr_t exact_consumer_caller = 0x14072CF47ull;
	inline constexpr std::uint32_t exact_consumer_target = 4;
	inline constexpr std::size_t scene_mip_srv_slot = 10;
	inline constexpr std::size_t scanned_srv_slots = 32;
	inline constexpr std::size_t constant_buffer_slots =
		D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
	inline constexpr std::size_t maximum_consumer_samples = 256;
	// The focused H2 owner pair currently emits about 12.4k write records before
	// the selected SSR consumer. Retain the entire pair so producer provenance is
	// not erased by the ring before the final read is inspected.
	inline constexpr std::size_t maximum_resource_write_records = 16384;
	inline constexpr std::size_t maximum_copy_source_lineage_samples = 16;
	inline constexpr std::size_t maximum_binding_name = 64;
	inline constexpr std::size_t maximum_shader_debug_name = 96;

	struct resource_descriptor
	{
		bool captured{};
		bool valid{};
		std::uint32_t dimension{};
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t depth{};
		std::uint32_t mip_levels{};
		std::uint32_t array_size{};
		std::uint32_t format{};
		std::uint32_t sample_count{};
		std::uint32_t sample_quality{};
		std::uint32_t usage{};
		std::uint32_t bind_flags{};
		std::uint32_t cpu_access_flags{};
		std::uint32_t misc_flags{};
		std::uint32_t byte_width{};
		std::uint32_t structure_byte_stride{};
	};

	struct shader_declaration
	{
		bool declared{};
		bool declared_in_reflection{};
		bool declared_in_disassembly{};
		bool recovered_from_disassembly{};
		std::uint32_t input_type{};
		std::uint32_t return_type{};
		std::uint32_t dimension{};
		std::uint32_t bind_point{};
		std::uint32_t bind_count{};
		std::uint64_t name_hash{};
		std::array<char, maximum_binding_name> name{};
	};

	struct srv_view_descriptor
	{
		std::uint32_t format{};
		std::uint32_t dimension{};
		std::uint32_t most_detailed_mip{};
		std::uint32_t mip_levels{};
		std::uint32_t first_array_slice{};
		std::uint32_t array_size{};
		std::uint32_t first_element{};
		std::uint32_t element_count{};
	};

	struct srv_sample
	{
		shader_declaration declaration{};
		std::uint32_t slot{};
		std::uintptr_t view{};
		std::uintptr_t resource{};
		srv_view_descriptor view_descriptor{};
		resource_descriptor resource_descriptor{};
		engine_stereo_ssr_consumer_window::resource_write_metadata last_write{};
	};

	struct constant_buffer_sample
	{
		shader_declaration declaration{};
		std::uint32_t slot{};
		std::uintptr_t buffer{};
		std::uint32_t byte_width{};
		engine_stereo_constant_buffer_probe::content_snapshot content{};
		engine_stereo_constant_buffer_probe::content_byte_snapshot content_bytes{};
		bool content_bytes_available{};
	};

	struct consumer_sample
	{
		std::uint64_t pair_id{};
		std::uint32_t eye{2};
		std::uint32_t output_target{0xFFFFFFFFu};
		std::uintptr_t caller{};
		std::uintptr_t shader{};
		std::uint64_t bytecode_hash{};
		std::uint64_t shader_debug_name_hash{};
		std::array<char, maximum_shader_debug_name> shader_debug_name{};
		bool reflection_resolved{};
		bool disassembly_resolved{};
		bool declarations_resolved{};
		bool ssr_name_match{};
		engine_stereo_dxbc_declarations::shader_profile shader_profile{
			engine_stereo_dxbc_declarations::shader_profile::unknown};
		bool scene_mip_candidate{};
		std::uint32_t declared_srv_count{};
		std::uint32_t declared_constant_buffer_count{};
		std::uint32_t bound_srv_count{};
		std::uint32_t bound_constant_buffer_count{};
		std::array<srv_sample, scanned_srv_slots> shader_resources{};
		std::array<constant_buffer_sample, constant_buffer_slots> constant_buffers{};
	};

	struct copy_source_lineage_sample
	{
		engine_stereo_ssr_consumer_window::resource_copy_source_lineage lineage{};
		resource_descriptor destination_descriptor{};
		resource_descriptor source_descriptor{};
	};

	struct report
	{
		engine_stereo_ssr_consumer_window::state window{};
		engine_stereo_ssr_consumer_window::focused_state focused{};
		bool installed{};
		bool observer_attached{};
		bool content_tracking_active{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint32_t owner_thread{};
		std::uint32_t active_eye{2};
		std::uint64_t active_pair{};
		std::uint64_t callback_entries{};
		std::uint64_t caller_rejections{};
		std::uint64_t target_rejections{};
		std::uint64_t inactive_rejections{};
		std::uint64_t context_rejections{};
		std::uint64_t thread_rejections{};
		std::uint64_t eye_rejections{};
		std::uint64_t shader_queries{};
		std::uint64_t shader_missing{};
		std::uint64_t bytecode_missing{};
		std::uint64_t bytecode_oversized{};
		std::uint64_t reflection_failures{};
		std::uint64_t reflection_empty{};
		std::uint64_t reflection_cache_overflows{};
		std::uint64_t disassembly_attempts{};
		std::uint64_t disassembly_failures{};
		std::uint64_t disassembly_unsupported_profiles{};
		std::uint64_t disassembly_recovered_srv_bindings{};
		std::uint64_t disassembly_recovered_constant_buffer_bindings{};
		std::uint64_t disassembly_out_of_range_declarations{};
		std::uint64_t disassembly_malformed_declarations{};
		std::uint64_t declared_srv_bindings{};
		std::uint64_t declared_constant_buffer_bindings{};
		std::uint64_t direct_bound_srv_bindings{};
		std::uint64_t direct_bound_constant_buffer_bindings{};
		std::uint64_t ssr_name_rejections{};
		std::uint64_t scene_mip_declaration_rejections{};
		std::uint64_t scene_mip_binding_rejections{};
		std::uint64_t scene_mip_resource_rejections{};
		std::uint64_t scene_mip_candidates{};
		std::uint64_t content_known{};
		std::uint64_t content_unknown{};
		std::uint64_t content_bytes_known{};
		std::uint64_t content_bytes_unavailable{};
		std::uint64_t resource_write_callbacks{};
		std::uint64_t resource_write_rejections{};
		std::uint64_t resource_write_records{};
		std::uint64_t resource_write_overflows{};
		std::uint64_t resource_write_opaque_barriers{};
		std::uint64_t resource_write_native_conversion_lists{};
		std::uint64_t declared_srv_last_write_known{};
		std::uint64_t declared_srv_last_write_unknown{};
		std::uint64_t copy_source_lineage_candidates{};
		std::uint64_t copy_source_lineage_known{};
		std::uint64_t copy_source_lineage_unknown{};
		std::uint64_t copy_source_lineage_overflows{};
		std::array<copy_source_lineage_sample,
			maximum_copy_source_lineage_samples> copy_source_lineage_samples{};
		std::size_t copy_source_lineage_sample_count{};
		std::uint64_t sample_overflows{};
		std::array<consumer_sample, maximum_consumer_samples> samples{};
		std::size_t sample_count{};
	};

	[[nodiscard]] bool install(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;

	// The bounded discovery probe publishes pointer identities for every shader
	// that independently proved the SSR-name, t10-declaration and live t10-binding
	// contract. The identities remain available after discovery detaches so later
	// explicitly armed diagnostics can filter the generic target-4 draw site
	// without retaining or dereferencing shader objects.
	[[nodiscard]] bool is_scene_mip_candidate_shader(
		ID3D11PixelShader* shader) noexcept;

	// The owner pass supplies the exact sequential replay lifetime. Seeded temporal
	// pairs are observed only by the CPU history probe and are never admitted here.
	// Discovery proves one stable SSR-named pixel-shader bytecode declared t10 in
	// executable DXBC and directly bound a valid scene-mip resource in both eyes
	// across two pairs. A focused follow-up pair then retains only that bytecode's
	// complete left/right SRV and constant-buffer snapshots. Content marked unknown
	// remains explicitly incomplete; neither phase proves which texture is the
	// offending SSR history.
	[[nodiscard]] bool begin_pair(std::uint64_t pair_id,
		ID3D11DeviceContext* context, std::uint64_t device_generation,
		std::uint32_t owner_thread, bool temporal_history_seeded) noexcept;
	[[nodiscard]] bool begin_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	void end_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	void end_pair(std::uint64_t pair_id, bool successful) noexcept;

	// Installed through engine_stereo_execution's existing independent
	// DrawIndexed observer. Calls outside the known scene-mip consumer call site
	// return before querying any D3D11 state.
	void observe_draw_indexed(ID3D11DeviceContext* context,
		std::uintptr_t caller, std::uint32_t output_target) noexcept;

	// Producer hooks call this after submitting a natural write. It records CPU
	// ordering metadata only and is a no-op outside the admitted focused pair.
	// `destination` and `source` are borrowed identities; this function never
	// retains or dereferences either resource. Reuse engine_stereo_execution,
	// engine_stereo_output_merger and engine_stereo_resource_ops publishers when
	// wiring it; do not stack another detour on the immediate context.
	void observe_resource_write(ID3D11DeviceContext* context,
		ID3D11Resource* destination,
		engine_stereo_ssr_consumer_window::resource_write_operation operation,
		std::uintptr_t caller,
		std::uint32_t destination_subresource =
			engine_stereo_ssr_consumer_window::all_resource_subresources,
		ID3D11Resource* source = nullptr,
		std::uint32_t source_subresource =
			engine_stereo_ssr_consumer_window::all_resource_subresources,
		std::uint32_t output_target =
			engine_stereo_ssr_consumer_window::unknown_output_target) noexcept;

	void get_report(report& output) noexcept;
}
