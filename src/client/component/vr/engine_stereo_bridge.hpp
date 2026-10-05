#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace vr::engine_stereo_bridge
{
	struct eye_projection
	{
		float tan_left{};
		float tan_right{};
		float tan_down{};
		float tan_up{};

		[[nodiscard]] float symmetric_tan_half_x() const noexcept;
		[[nodiscard]] float symmetric_tan_half_y() const noexcept;
		[[nodiscard]] float optical_aspect() const noexcept;
	};

	struct render_config
	{
		std::uint64_t frame_id{};
		std::array<eye_projection, 2> eyes{};
		float half_eye_offset_units{};
		bool swap_eyes{};
		std::uint64_t publication{};
		std::uint64_t pair_id{};
		std::uint32_t output_eye{};
		std::uint32_t view_eye{};
	};

	struct view_family
	{
		std::uint64_t frame_id{};
		std::uint64_t publication{};
		std::array<float, 3> left_position{};
		std::array<float, 3> right_position{};
		std::array<eye_projection, 2> eyes{};
	};

	struct capture_tag
	{
		bool valid{};
		std::uint64_t pair_id{};
		std::uint32_t eye_index{};
		bool native{};
	};

	enum class target_observation_phase : std::uint8_t
	{
		before_scene,
		after_scene,
	};

	// This is deliberately an observation contract.  A D3D11 render-target
	// pointer is only useful for the current engine device and must never be
	// persisted or used as a cross-thread ownership token.  The native renderer
	// will promote an observation to a target only after both eyes have been
	// observed for one immutable view family.
	struct target_observation
	{
		std::uint64_t frame_id{};
		std::uint64_t pair_id{};
		std::uint32_t output_eye{};
		target_observation_phase phase{target_observation_phase::before_scene};
		std::uintptr_t resource{};
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t format{};
		std::uint32_t sample_count{};
		bool valid{};
	};

	struct status
	{
		bool target_matched{};
		bool enabled{};
		bool views_available{};
		bool swap_eyes{};
		float ipd_meters{};
		float world_scale{};
		float half_eye_offset_units{};
		std::array<eye_projection, 2> eyes{};
		std::uint64_t view_publications{};
		std::uint64_t view_family_id{};
		std::uint64_t stereo_frames{};
		std::uint64_t coherent_stereo_pairs{};
		std::uint64_t incoherent_stereo_pairs{};
		std::uint64_t scene_hook_entries{};
		std::uint64_t scene_calls{};
		std::uint64_t invalid_viewports{};
		std::uint64_t restore_conflicts{};
		std::uint64_t target_observations{};
		std::uint64_t target_misses{};
		std::uint64_t distinct_target_pairs{};
		std::uint64_t aliased_target_pairs{};
		std::uint64_t native_target_candidate_frame{};
		target_observation last_target{};
		bool render_hook_installed{};
		std::uint16_t last_full_width{};
		std::uint16_t last_full_height{};
		std::uint16_t last_left_width{};
		std::uint16_t last_right_width{};
	};

	void configure_target(bool matched) noexcept;
	void set_enabled(bool enabled) noexcept;
	void set_world_scale(float units_per_meter) noexcept;
	void set_swap_eyes(bool enabled) noexcept;
	[[nodiscard]] bool publish_view_family(std::uint64_t frame_id,
		const std::array<float, 3>& left_position,
		const std::array<float, 3>& right_position,
		const std::array<eye_projection, 2>& projections) noexcept;
	void publish_views(const std::array<float, 3>& left_position,
		const std::array<float, 3>& right_position,
		const std::array<eye_projection, 2>& projections) noexcept;
	void invalidate_views() noexcept;
	void reset() noexcept;

	[[nodiscard]] bool is_active() noexcept;
	[[nodiscard]] bool get_view_family(view_family& output) noexcept;
	// Eye identity must come from a complete H2 scene-record/slot transaction and
	// an explicit projection-to-eye mapping. There is intentionally no implicit
	// "next eye" API: natural scene-call order is not a stereo contract and may
	// cross world snapshots.
	[[nodiscard]] bool get_render_config_for_eye(std::uint32_t output_eye,
		render_config& output) noexcept;
	// Snapshot both eyes under one lock. A stereo renderer must never combine eye
	// configurations from different OpenVR publications.
	[[nodiscard]] bool get_render_configs(
		std::array<render_config, 2>& output) noexcept;
	[[nodiscard]] capture_tag consume_capture_tag() noexcept;
	void set_render_hook_installed(bool installed) noexcept;
	void record_scene_hook_entry() noexcept;
	void record_stereo_eye(const render_config& config, std::uint16_t full_width,
		std::uint16_t full_height, std::uint16_t eye_width, std::uint16_t eye_height,
		bool native = false) noexcept;
	void record_invalid_viewport() noexcept;
	void record_restore_conflict() noexcept;
	void record_render_target_observation(const render_config& config,
		 target_observation_phase phase, std::uintptr_t resource,
		 std::uint32_t width, std::uint32_t height, std::uint32_t format,
		 std::uint32_t sample_count) noexcept;
	void record_missing_render_target(const render_config& config,
		 target_observation_phase phase) noexcept;
	[[nodiscard]] status get_status() noexcept;
}
