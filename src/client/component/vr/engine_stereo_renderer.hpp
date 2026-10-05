#pragma once

#include <cstdint>

namespace vr::engine_stereo_renderer
{
	struct scene_handoff_status { std::uint64_t attempts{}, publications{}, failures{}; };
	[[nodiscard]] scene_handoff_status get_scene_handoff_status() noexcept;
	struct culling_union_status
	{
		std::uint64_t attempts{};
		std::uint64_t applications{};
		std::uint64_t failures{};
		float tan_left{};
		float tan_right{};
		float tan_down{};
		float tan_up{};
		float near_distance_units{};
		float horizontal_origin_expansion{};
		std::uint64_t fx_attempts{};
		std::uint64_t fx_applications{};
		std::uint64_t fx_failures{};
	};

	[[nodiscard]] culling_union_status get_culling_union_status() noexcept;

	struct execution_evidence_acceptance
	{
		bool prerequisite_observation_complete{};
		bool backend_target_frame_complete{};
		bool output_merger_complete{};
		bool draw_indexed_complete{};
		bool execution_census_complete{};
		bool terminal_device_identity_consistent{};
	};

	[[nodiscard]] constexpr bool accepts_complete_execution_evidence(
		const execution_evidence_acceptance& evidence) noexcept
	{
		return evidence.prerequisite_observation_complete &&
			evidence.backend_target_frame_complete &&
			evidence.output_merger_complete && evidence.draw_indexed_complete &&
			evidence.execution_census_complete &&
			evidence.terminal_device_identity_consistent;
	}

	enum class camera_observation_source : std::uint8_t
	{
		set_viewpos_now,
		camera_helper,
	};

	enum class camera_observation_stage : std::uint8_t
	{
		enter,
		before_original,
		after_original,
		after_mod,
		leave,
	};

	// CPU-only observation around H2's real R_EndFrame call. These boundaries are
	// correlation epochs, not proof that two scene calls share a world snapshot.
	void begin_r_end_frame() noexcept;
	void end_r_end_frame() noexcept;

	// CPU-only snapshots around the two H2 calls adjacent to R_EndFrame in the
	// recovered top-level renderer sequence. These observers never reserve scene
	// records, invoke R_RenderScene, or change frontend/backend ownership.
	void begin_frame_state_transition() noexcept;
	void end_frame_state_transition() noexcept;
	void begin_frontend_handoff() noexcept;
	void end_frontend_handoff() noexcept;

	// Called only from camera.cpp's already-installed hooks. This records hashes
	// and pointer identities; it never changes camera state or installs another
	// patch. input/output may be null when that address has no recovered extent.
	void observe_camera_state(camera_observation_source source,
		camera_observation_stage stage, const void* input, const void* output,
		float scalar, std::uintptr_t caller) noexcept;

	// Called only by the diagnostics control thread. Renderer/backend hooks publish
	// bounded one-shot CPU snapshots into fixed storage; this checkpoint performs
	// all file I/O and emits a complete evidence bundle without blocking H2's
	// frontend, backend, Present, or OpenVR owners.
	// Returns true only when the current terminal execution report and both its
	// dedicated and aggregate manifests have been atomically persisted. A stale
	// file from an earlier H2-MOD process never satisfies this result.
	[[nodiscard]] bool checkpoint_observation_artifacts() noexcept;
}
