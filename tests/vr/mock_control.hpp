#pragma once

#include <Windows.h>
#include <array>
#include <d3d11.h>

namespace vr::tests::mock
{
	enum class scenario : std::uint32_t
	{
		happy,
		canted_views,
		parallel_views,
		scaled_view_quaternions,
		runtime_unavailable,
		no_hmd,
		graphics_mismatch,
		swapchain_image_failure,
		wait_timeout,
		destroy_instance_proc_null,
	};

	enum class failure_point : std::uint32_t
	{
		none,
		acquire_swapchain_image,
		wait_swapchain_image,
		release_swapchain_image,
		destroy_swapchain,
		destroy_space,
		destroy_session,
		destroy_instance,
		end_session,
		get_current_interaction_profile,
	};

	struct statistics
	{
		std::uint64_t action_sets_created{}, action_sets_destroyed{}, actions_created{}, actions_destroyed{},
		    binding_profiles{}, action_syncs{}, haptic_events{};
		std::uint64_t quad_frames{}, cylinder_frames{};
		std::array<std::uint32_t, 2> last_projection_pixels{};
		std::uint32_t last_menu_pixel{};
		std::uint64_t instances_created{};
		std::uint64_t instances_with_runtime_override{};
		std::uint64_t instances_destroyed{};
		std::uint64_t sessions_created{};
		std::uint64_t sessions_destroyed{};
		std::uint64_t spaces_created{};
		std::uint64_t spaces_destroyed{};
		std::uint64_t swapchains_created{};
		std::uint64_t swapchains_destroyed{};
		std::uint64_t destroy_while_acquired_rejected{};
		std::uint64_t textures_created{};
		std::uint64_t sessions_begun{};
		std::uint64_t sessions_ended{};
		std::uint64_t invalid_session_end_rejected{};
		std::uint64_t frames_waited{};
		std::uint64_t frames_begun{};
		std::uint64_t frames_ended{};
		std::uint64_t projection_frames{};
		std::uint64_t zero_layer_frames{};
		std::uint64_t eye_color_frames_validated{};
		std::uint64_t views_located{};
		std::uint64_t images_acquired{};
		std::uint64_t images_waited{};
		std::uint64_t images_released{};
		std::uint64_t texture_write_epochs_validated{};
	};

	using reset_fn = void(WINAPI*)();
	using set_scenario_fn = void(WINAPI*)(scenario);
	using fail_once_fn = void(WINAPI*)(failure_point, std::int32_t);
	using destroy_acquired_fn = std::int32_t(WINAPI*)();
	using set_graphics_requirements_fn = void(WINAPI*)(LUID, D3D_FEATURE_LEVEL);
	using set_should_render_fn = void(WINAPI*)(BOOL);
	using set_eye_extent_fn = void(WINAPI*)(std::uint32_t, std::uint32_t);
	using set_cylinder_supported_fn = void(WINAPI*)(BOOL);
	using set_synthetic_checks_fn = void(WINAPI*)(BOOL);
	using set_action_value_fn = void(WINAPI*)(const char*, float, float);
	using set_view_flags_fn = void(WINAPI*)(std::uint64_t);
	using set_head_height_fn = void(WINAPI*)(float);
	using set_runtime_name_fn = void(WINAPI*)(const char*);
	using set_interaction_profile_fn = void(WINAPI*)(unsigned, const char*);
	using queue_session_state_fn = void(WINAPI*)(std::int32_t);
	using get_statistics_fn = void(WINAPI*)(statistics*);
} // namespace vr::tests::mock
