#pragma once

// Internal producer/diagnostic boundary. Producers publish bounded CPU copies;
// the control thread alone formats reports and writes files. A ready capture is
// immutable for this process. This storage never owns native or GPU resources.
#include "../native_render_contract.hpp"
#include "../engine_view_probe.hpp"
#include <array>
#include <atomic>
#include <limits>

namespace vr::diagnostics::renderer_evidence
{
	inline std::uint64_t content_signature(const std::uintptr_t address, const std::size_t size) noexcept
	{
		if (address == 0 || size == 0)
			return 0;
		constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
		constexpr std::uint64_t fnv_prime = 1099511628211ull;
		auto hash = fnv_offset;
		const auto* const bytes = reinterpret_cast<const volatile unsigned char*>(address);
		for (std::size_t index{}; index < size; ++index)
		{
			hash ^= bytes[index];
			hash *= fnv_prime;
		}
		return hash;
	}

	inline constexpr std::size_t evidence_metadata_count = 8;

	template <std::size_t Size> struct one_shot_artifact
	{
		// 0=empty, 1=producer copying, 2=immutable and readable. File I/O is
		// deliberately tracked separately and occurs only on the diagnostics thread.
		std::atomic_uint32_t state{};
		std::array<std::uint8_t, Size> bytes{};
		std::array<std::uint64_t, evidence_metadata_count> metadata{};
		std::atomic_bool written{};
	};
	struct ownership_boundary_artifact
	{
		std::atomic_uint32_t state{};
		engine_view_probe::ownership_boundary_observation observation{};
		std::uint64_t frontend_frame_id{};
		std::uint64_t capture_tick{};
		std::uint32_t thread_id{};
	};

	inline constexpr std::size_t ownership_boundary_count = 3;
	inline constexpr std::size_t ownership_boundary_stage_count = 2;
	inline constexpr std::size_t scene_descriptor_bytes = 0x501C8;
	struct capture_store
	{
		one_shot_artifact<native_render_contract::target_registry_size> baseline_target_registry_artifact{};
		one_shot_artifact<scene_descriptor_bytes> descriptor_before_artifact{};
		one_shot_artifact<scene_descriptor_bytes> descriptor_after_artifact{};
		one_shot_artifact<engine_view_probe::frontend_slot_stride> slot_before_initializer_artifact{};
		one_shot_artifact<engine_view_probe::frontend_slot_stride> slot_after_initializer_artifact{};
		std::array<one_shot_artifact<engine_view_probe::frontend_slot_stride>, 2> stereo_eye_slot_artifacts{};
		std::atomic_uint32_t stereo_eye_slot_capture_state{};
		one_shot_artifact<engine_view_probe::frontend_slot_stride> backend_view_source_before_artifact{};
		one_shot_artifact<engine_view_probe::frontend_slot_stride> backend_view_source_after_artifact{};
		std::array<one_shot_artifact<engine_view_probe::frontend_slot_stride>, 2>
		    backend_bound_eye_slot_artifacts{};
		one_shot_artifact<engine_view_probe::frontend_slot_stride> slot_after_generator_artifact{};
		one_shot_artifact<engine_view_probe::frontend_slot_stride> output_after_generator_artifact{};
		one_shot_artifact<engine_view_probe::frontend_record_stride> record_before_target_prepare_artifact{};
		one_shot_artifact<engine_view_probe::frontend_record_stride> record_after_target_prepare_artifact{};
		one_shot_artifact<native_render_contract::target_registry_size>
		    registry_before_target_prepare_artifact{};
		one_shot_artifact<native_render_contract::target_registry_size>
		    registry_after_target_prepare_artifact{};

		std::array<ownership_boundary_artifact, ownership_boundary_count * ownership_boundary_stage_count>
		    ownership_boundary_artifacts{};
		std::atomic_uint64_t ownership_boundary_capture_frame{};
	};
	inline capture_store captures;
	// Control-thread only. True requires this process to atomically persist the
	// current terminal execution report and both dedicated/aggregate manifests.
	// File existence alone never establishes current-process evidence.
	[[nodiscard]] bool checkpoint() noexcept;
}

namespace vr::engine_stereo_renderer
{
	// Existing control-thread baseline copy, using the same native read guards.
	void capture_registry_baseline() noexcept;
}
