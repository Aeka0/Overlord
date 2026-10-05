#pragma once

#include "engine_stereo_resource_ops.hpp"

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_material_buffer_probe
{
	enum class state : std::uint8_t
	{
		idle,
		recording,
		pending,
		complete,
		failed,
	};

	enum class failure : std::uint8_t
	{
		none,
		lifecycle,
		context,
		thread,
		resource_creation,
		resource_contract,
		capture_capacity,
		reference_capacity,
		copy,
		query,
		map,
		count_mismatch,
		missing_capture,
	};

	inline constexpr std::uint32_t material_buffer_bytes = 1088;
	inline constexpr std::size_t maximum_captures_per_eye = 512;
	inline constexpr std::size_t maximum_references = 512;
	inline constexpr std::size_t maximum_difference_offsets = 32;

	struct comparison
	{
		std::uint64_t output_ordinal{};
		std::array<std::uint64_t, 2> output_ordinals{};
		std::uint64_t output0_family_ordinal{};
		std::uint64_t output1_family_ordinal{};
		std::array<std::uint64_t, 2> upload_generations{};
		std::array<std::uintptr_t, 2> buffer_identities{};
		std::array<std::uint64_t, 2> content_hashes{};
		std::array<std::uint32_t, 2> capture_ordinals{};
		std::uint32_t compared_bytes{};
		std::uint32_t differing_bytes{};
		std::uint32_t first_difference{};
		std::uint32_t last_difference{};
		std::uint8_t family{};
		std::uint8_t stage{};
		std::uint8_t slot{};
		bool output0_resolved{};
		bool output1_resolved{};
		std::array<std::uint16_t, maximum_difference_offsets> difference_offsets{};
		std::array<std::uint8_t, maximum_difference_offsets> output0_values{};
		std::array<std::uint8_t, maximum_difference_offsets> output1_values{};
		std::size_t difference_offset_count{};
	};

	struct report
	{
		state current{state::idle};
		failure error{failure::none};
		std::uint64_t pair_id{};
		std::uint64_t device_generation{};
		std::uintptr_t context{};
		std::uint32_t owner_thread{};
		std::uint32_t active_eye{2};
		std::uint8_t completed_eye_mask{};
		std::array<std::uint32_t, 2> captures{};
		std::uint64_t capture_attempts{};
		std::uint64_t capture_completions{};
		std::uint64_t capture_contract_rejections{};
		std::uint64_t capture_overflows{};
		std::uint64_t bound_capture_attempts{};
		std::uint64_t bound_capture_completions{};
		std::uint64_t capture_deduplications{};
		std::uint64_t reference_attempts{};
		std::uint64_t reference_completions{};
		std::uint64_t reference_overflows{};
		std::uint64_t retirement_polls{};
		std::uint64_t not_ready_polls{};
		std::uint64_t comparisons{};
		std::uint64_t identical{};
		std::uint64_t different{};
		std::uint64_t unresolved_output0{};
		std::uint64_t unresolved_output1{};
		std::uint64_t unreferenced_captures{};
		bool capture_count_mismatch{};
		HRESULT staging_create_result{E_PENDING};
		HRESULT query_create_result{E_PENDING};
		HRESULT query_result{E_PENDING};
		HRESULT map_result{E_PENDING};
		std::array<comparison, maximum_references> samples{};
		std::size_t sample_count{};
	};

	[[nodiscard]] bool begin_pair(std::uint64_t pair_id,
		ID3D11DeviceContext* context, std::uint64_t device_generation,
		std::uint32_t owner_thread) noexcept;
	[[nodiscard]] bool begin_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	[[nodiscard]] bool end_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	void observe_resource_operation(
		const engine_stereo_resource_ops::event& event) noexcept;
	// Capture the exact constant-buffer contents visible to a classified draw.
	// This is the authoritative path for buffers uploaded before the observed
	// stereo pair; upload-time observation alone cannot recover those contents.
	void capture_bound_buffer(ID3D11DeviceContext* context, std::uint32_t eye,
		std::uint64_t output_ordinal, ID3D11Buffer* buffer,
		std::uint64_t upload_generation,
		std::uintptr_t draw_caller) noexcept;
	// Called by the already-paired dynamic-FX census. The numeric family is kept
	// independent of that module's enum to avoid a diagnostics dependency cycle.
	void note_dynamic_fx_reference(std::uint8_t family,
		std::uint64_t output0_output_ordinal,
		std::uint64_t output1_output_ordinal,
		std::uint64_t output0_family_ordinal,
		std::uint64_t output1_family_ordinal,
		std::uint8_t stage, std::uint8_t slot,
		std::uintptr_t output0_buffer_identity,
		std::uintptr_t output1_buffer_identity,
		std::uint64_t output0_upload_generation,
		std::uint64_t output1_upload_generation) noexcept;
	[[nodiscard]] bool end_pair(std::uint64_t pair_id) noexcept;
	// Owner-thread polling only; both calls are strictly DONOTFLUSH/nonblocking.
	void poll(ID3D11DeviceContext* context, std::uint64_t device_generation,
		std::uint32_t owner_thread) noexcept;
	void cancel(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	[[nodiscard]] report get_report() noexcept;
	[[nodiscard]] const char* to_string(state value) noexcept;
	[[nodiscard]] const char* to_string(failure value) noexcept;
}
