#include <std_include.hpp>

#include "engine_stereo_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace vr::engine_stereo_bridge
{
	namespace
	{
		constexpr float default_ipd_meters = 0.064f;
		constexpr float default_world_scale = 39.3700787f;

		std::mutex state_mutex;
		bool target_matched{};
		bool enabled{};
		bool views_available{};
		bool swap_eyes{};
		bool render_hook_installed{};
		float ipd_meters{default_ipd_meters};
		float world_scale{default_world_scale};
		std::array<eye_projection, 2> eye_projections{};
		std::array<float, 3> left_eye_position{};
		std::array<float, 3> right_eye_position{};
		std::uint64_t view_publications{};
		std::uint64_t published_frame_id{};
		std::uint32_t published_eye_mask{};
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
		std::array<target_observation, 2> pair_targets{};
		std::uint64_t pair_target_id{};
		target_observation last_target{};
		std::uint16_t last_full_width{};
		std::uint16_t last_full_height{};
		std::uint16_t last_left_width{};
		std::uint16_t last_right_width{};
		std::uint64_t last_render_frame_id{};
		capture_tag pending_capture{};

		bool finite_vector(const std::array<float, 3>& value) noexcept
		{
			return std::ranges::all_of(value, [](const float element)
			{
				return std::isfinite(element);
			});
		}

		bool valid_projection(const eye_projection& projection) noexcept
		{
			const auto half_x = projection.symmetric_tan_half_x();
			const auto half_y = projection.symmetric_tan_half_y();
			return std::isfinite(projection.tan_left) &&
				std::isfinite(projection.tan_right) &&
				std::isfinite(projection.tan_down) &&
				std::isfinite(projection.tan_up) &&
				projection.tan_left < 0.0f && projection.tan_right > 0.0f &&
				projection.tan_down < 0.0f && projection.tan_up > 0.0f &&
				projection.tan_left < projection.tan_right &&
				projection.tan_down < projection.tan_up &&
				half_x >= 0.1f && half_x <= 10.0f &&
				half_y >= 0.1f && half_y <= 10.0f;
		}

		void invalidate_views_locked() noexcept
		{
			views_available = false;
			eye_projections = {};
			published_eye_mask = 0;
			pending_capture = {};
			pair_targets = {};
			pair_target_id = 0;
			last_target = {};
		}
	}

	float eye_projection::symmetric_tan_half_x() const noexcept
	{
		return (std::max)(-tan_left, tan_right);
	}

	float eye_projection::symmetric_tan_half_y() const noexcept
	{
		return (std::max)(-tan_down, tan_up);
	}

	float eye_projection::optical_aspect() const noexcept
	{
		const auto vertical_range = tan_up - tan_down;
		return vertical_range > 0.0f ? (tan_right - tan_left) / vertical_range : 0.0f;
	}

	void configure_target(const bool matched) noexcept
	{
		const std::lock_guard lock(state_mutex);
		target_matched = matched;
		if (!matched)
		{
			enabled = false;
			invalidate_views_locked();
		}
	}

	void set_enabled(const bool requested) noexcept
	{
		const std::lock_guard lock(state_mutex);
		enabled = requested && target_matched;
		if (!enabled)
		{
			invalidate_views_locked();
		}
	}

	void set_world_scale(const float units_per_meter) noexcept
	{
		if (!std::isfinite(units_per_meter) || units_per_meter <= 0.0f || units_per_meter > 10000.0f)
		{
			return;
		}

		const std::lock_guard lock(state_mutex);
		world_scale = units_per_meter;
	}

	void set_swap_eyes(const bool requested) noexcept
	{
		const std::lock_guard lock(state_mutex);
		swap_eyes = requested;
	}

	bool publish_view_family(const std::uint64_t frame_id,
		const std::array<float, 3>& left_position,
		const std::array<float, 3>& right_position,
		const std::array<eye_projection, 2>& projections) noexcept
	{
		if (frame_id == 0 || !finite_vector(left_position) || !finite_vector(right_position) ||
			!valid_projection(projections[0]) || !valid_projection(projections[1]))
		{
			invalidate_views();
			return false;
		}

		const float dx = right_position[0] - left_position[0];
		const float dy = right_position[1] - left_position[1];
		const float dz = right_position[2] - left_position[2];
		const float separation = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (!std::isfinite(separation) || separation < 0.02f || separation > 0.12f)
		{
			invalidate_views();
			return false;
		}

		const std::lock_guard lock(state_mutex);
		if (!target_matched || !enabled)
		{
			return false;
		}
		if (published_eye_mask != 0 && published_eye_mask != 0x3u)
		{
			// Keep one predicted family immutable until both H2 scene submissions
			// have arrived. Advancing here would pair unrelated left/right images.
			return false;
		}

		ipd_meters = separation;
		left_eye_position = left_position;
		right_eye_position = right_position;
		eye_projections = projections;
		published_frame_id = frame_id;
		published_eye_mask = 0;
		views_available = true;
		++view_publications;
		return true;
	}

	void publish_views(const std::array<float, 3>& left_position,
		const std::array<float, 3>& right_position,
		const std::array<eye_projection, 2>& projections) noexcept
	{
		std::uint64_t frame_id{};
		{
			const std::lock_guard lock(state_mutex);
			frame_id = published_frame_id + 1;
		}
		(void)publish_view_family(frame_id, left_position, right_position, projections);
	}

	void invalidate_views() noexcept
	{
		const std::lock_guard lock(state_mutex);
		invalidate_views_locked();
	}

	void reset() noexcept
	{
		const std::lock_guard lock(state_mutex);
		enabled = false;
		views_available = false;
		swap_eyes = false;
		ipd_meters = default_ipd_meters;
		world_scale = default_world_scale;
		eye_projections = {};
		left_eye_position = {};
		right_eye_position = {};
		view_publications = 0;
		published_frame_id = 0;
		published_eye_mask = 0;
		stereo_frames = 0;
		coherent_stereo_pairs = 0;
		incoherent_stereo_pairs = 0;
		scene_hook_entries = 0;
		scene_calls = 0;
		invalid_viewports = 0;
		restore_conflicts = 0;
		target_observations = 0;
		target_misses = 0;
		distinct_target_pairs = 0;
		aliased_target_pairs = 0;
		native_target_candidate_frame = 0;
		pair_targets = {};
		pair_target_id = 0;
		last_target = {};
		last_full_width = 0;
		last_full_height = 0;
		last_left_width = 0;
		last_right_width = 0;
		last_render_frame_id = 0;
		pending_capture = {};
		published_eye_mask = 0;
	}

	bool is_active() noexcept
	{
		const std::lock_guard lock(state_mutex);
		return target_matched && enabled && views_available && render_hook_installed;
	}

	bool get_view_family(view_family& output) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (!target_matched || !enabled || !views_available || published_frame_id == 0)
		{
			return false;
		}
		output.frame_id = published_frame_id;
		output.publication = view_publications;
		output.left_position = left_eye_position;
		output.right_position = right_eye_position;
		output.eyes = eye_projections;
		return true;
	}

	bool get_render_config_for_eye(const std::uint32_t requested_eye,
		render_config& output) noexcept
	{
		if (requested_eye >= 2) return false;
		const std::lock_guard lock(state_mutex);
		if (!target_matched || !enabled || !views_available || !render_hook_installed ||
			published_frame_id == 0)
		{
			return false;
		}
		output.frame_id = published_frame_id;
		output.eyes = eye_projections;
		output.half_eye_offset_units = ipd_meters * 0.5f * world_scale;
		output.swap_eyes = swap_eyes;
		output.publication = view_publications;
		output.output_eye = requested_eye;
		output.view_eye = swap_eyes ? requested_eye ^ 1u : requested_eye;
		output.pair_id = published_frame_id;
		return std::isfinite(output.half_eye_offset_units) &&
			output.half_eye_offset_units > 0.0f && output.half_eye_offset_units <= 1000.0f;
	}

	bool get_render_configs(std::array<render_config, 2>& output) noexcept
	{
		const std::lock_guard lock(state_mutex);
		output = {};
		if (!target_matched || !enabled || !views_available || !render_hook_installed ||
			published_frame_id == 0)
		{
			return false;
		}
		const auto half_offset = ipd_meters * 0.5f * world_scale;
		if (!std::isfinite(half_offset) || half_offset <= 0.0f || half_offset > 1000.0f)
		{
			return false;
		}
		for (std::uint32_t eye{}; eye < output.size(); ++eye)
		{
			output[eye] = {
				published_frame_id,
				eye_projections,
				half_offset,
				swap_eyes,
				view_publications,
				published_frame_id,
				eye,
				swap_eyes ? eye ^ 1u : eye,
			};
		}
		return true;
	}

	capture_tag consume_capture_tag() noexcept
	{
		const std::lock_guard lock(state_mutex);
		const auto result = pending_capture;
		pending_capture = {};
		return result;
	}

	void set_render_hook_installed(const bool installed) noexcept
	{
		const std::lock_guard lock(state_mutex);
		render_hook_installed = installed;
		if (!installed)
		{
			invalidate_views_locked();
		}
	}

	void record_scene_hook_entry() noexcept
	{
		const std::lock_guard lock(state_mutex);
		++scene_hook_entries;
	}

	void record_stereo_eye(const render_config& config, const std::uint16_t full_width,
		const std::uint16_t full_height, const std::uint16_t eye_width,
		const std::uint16_t eye_height, const bool native) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (config.output_eye == 1)
		{
			++stereo_frames;
			if (config.frame_id != 0 && config.frame_id == last_render_frame_id)
			{
				++coherent_stereo_pairs;
			}
			else
			{
				++incoherent_stereo_pairs;
			}
		}
		last_render_frame_id = config.frame_id;
		if (config.frame_id == published_frame_id && config.output_eye < 2)
		{
			published_eye_mask |= 1u << config.output_eye;
		}
		++scene_calls;
		last_full_width = full_width;
		last_full_height = full_height;
		last_left_width = eye_width;
		last_right_width = eye_height;
		pending_capture = {true, config.pair_id, config.output_eye, native};
	}

	void record_invalid_viewport() noexcept
	{
		const std::lock_guard lock(state_mutex);
		++invalid_viewports;
	}

	void record_restore_conflict() noexcept
	{
		const std::lock_guard lock(state_mutex);
		++restore_conflicts;
	}

	void record_render_target_observation(const render_config& config,
		const target_observation_phase phase, const std::uintptr_t resource,
		const std::uint32_t width, const std::uint32_t height,
		const std::uint32_t format, const std::uint32_t sample_count) noexcept
	{
		const std::lock_guard lock(state_mutex);
		++target_observations;
		const target_observation observation{
			config.frame_id,
			config.pair_id,
			config.output_eye,
			phase,
			resource,
			width,
			height,
			format,
			sample_count,
			resource != 0 && width != 0 && height != 0 && sample_count != 0,
		};
		last_target = observation;
		if (!observation.valid || observation.output_eye >= 2 || observation.pair_id == 0)
		{
			return;
		}

		if (pair_target_id != observation.pair_id)
		{
			pair_target_id = observation.pair_id;
			pair_targets = {};
		}
		const auto eye = observation.output_eye;
		pair_targets[eye] = observation;
		const auto& other = pair_targets[eye ^ 1u];
		if (!other.valid || other.pair_id != observation.pair_id ||
			other.frame_id != observation.frame_id ||
			other.phase != observation.phase)
		{
			return;
		}

		if (other.resource == observation.resource)
		{
			++aliased_target_pairs;
			return;
		}

		++distinct_target_pairs;
		// Distinct targets with matching dimensions and format, observed after
		// both scene submissions, are the minimum evidence for the native path.
		// This is diagnostic evidence only; it never enables rendering by itself.
		if (observation.phase == target_observation_phase::after_scene &&
			other.width == observation.width && other.height == observation.height &&
			other.format == observation.format &&
			other.sample_count == observation.sample_count)
		{
			native_target_candidate_frame = observation.frame_id;
		}
	}

	void record_missing_render_target(const render_config& config,
		const target_observation_phase phase) noexcept
	{
		const std::lock_guard lock(state_mutex);
		++target_misses;
		last_target = {
			config.frame_id,
			config.pair_id,
			config.output_eye,
			phase,
			0,
			0,
			0,
			0,
			0,
			false,
		};
	}

	status get_status() noexcept
	{
		const std::lock_guard lock(state_mutex);
		return {
			target_matched,
			enabled,
			views_available,
			swap_eyes,
			ipd_meters,
			world_scale,
			ipd_meters * 0.5f * world_scale,
			eye_projections,
			view_publications,
			published_frame_id,
			stereo_frames,
			coherent_stereo_pairs,
			incoherent_stereo_pairs,
			scene_hook_entries,
			scene_calls,
			invalid_viewports,
			restore_conflicts,
			target_observations,
			target_misses,
			distinct_target_pairs,
			aliased_target_pairs,
			native_target_candidate_frame,
			last_target,
			render_hook_installed,
			last_full_width,
			last_full_height,
			last_left_width,
			last_right_width,
		};
	}
}
