#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::controller_input
{
	enum class input_backend : std::uint8_t
	{
		unknown,
		openvr,
		openxr
	};
	enum class input_reason : std::uint8_t
	{
		none,
		not_sampled,
		not_initialized,
		initialization_failed,
		input_unavailable,
		action_update_failed,
		action_query_failed,
		action_inactive,
		controller_disconnected,
		pose_invalid,
		hmd_disconnected,
		hmd_pose_invalid,
		tracking_failed,
		compositor_focus_lost,
		runtime_reset,
		session_inactive,
		frame_not_rendered,
		profile_query_failed,
		profile_mismatch,
		calibration_invalid,
		pose_filter_rejected,
		action_value_invalid,
		reference_changed,
		view_configuration_unsupported,
		wait_frame_failed,
		begin_frame_failed,
		present_mismatch,
		frame_submission_failed
	};
	enum class input_channel : std::uint8_t
	{
		focus,
		move,
		turn,
		left_grip,
		right_grip,
		left_aim,
		right_aim,
		sprint,
		jump,
		left_trigger,
		right_trigger,
		left_squeeze,
		right_squeeze,
		count
	};
	inline constexpr auto input_channel_count = static_cast<std::size_t>(input_channel::count);
	constexpr std::size_t index(input_channel channel) noexcept
	{
		return static_cast<std::size_t>(channel);
	}
	constexpr input_channel hand_channel(input_channel left, unsigned hand) noexcept
	{
		return static_cast<input_channel>(index(left) + hand);
	}
	struct input_condition
	{
		input_reason reason{input_reason::not_sampled};
		std::int64_t code{}; // Backend API result; interpreted together with backend and reason.
	};
	struct input_observation
	{
		input_backend backend{};
		bool runtime_focus{}; // Availability before action synchronization, separate from frame.focused.
		bool runtime_focus_known{}; // False when a lifecycle/tracking failure prevented the query.
		input_condition gate{};
		std::array<input_condition, input_channel_count> channels{};
	};
	constexpr bool is_api_failure(input_reason reason) noexcept
	{
		switch (reason)
		{
		case input_reason::action_update_failed:
		case input_reason::action_query_failed:
		case input_reason::tracking_failed:
		case input_reason::profile_query_failed:
		case input_reason::initialization_failed:
		case input_reason::wait_frame_failed:
		case input_reason::begin_frame_failed:
		case input_reason::present_mismatch:
		case input_reason::frame_submission_failed:
			return true;
		default:
			return false;
		}
	}
	inline const char* to_string(input_backend value) noexcept
	{
		switch (value)
		{
		case input_backend::openvr:
			return "openvr";
		case input_backend::openxr:
			return "openxr";
		default:
			return "unknown";
		}
	}
	inline const char* to_string(input_reason value) noexcept
	{
		switch (value)
		{
#define VR_INPUT_REASON(name)                                                                                \
	case input_reason::name:                                                                                 \
		return #name
			VR_INPUT_REASON(none);
			VR_INPUT_REASON(not_sampled);
			VR_INPUT_REASON(not_initialized);
			VR_INPUT_REASON(initialization_failed);
			VR_INPUT_REASON(input_unavailable);
			VR_INPUT_REASON(action_update_failed);
			VR_INPUT_REASON(action_query_failed);
			VR_INPUT_REASON(action_inactive);
			VR_INPUT_REASON(controller_disconnected);
			VR_INPUT_REASON(pose_invalid);
			VR_INPUT_REASON(hmd_disconnected);
			VR_INPUT_REASON(hmd_pose_invalid);
			VR_INPUT_REASON(tracking_failed);
			VR_INPUT_REASON(compositor_focus_lost);
			VR_INPUT_REASON(runtime_reset);
			VR_INPUT_REASON(session_inactive);
			VR_INPUT_REASON(frame_not_rendered);
			VR_INPUT_REASON(profile_query_failed);
			VR_INPUT_REASON(profile_mismatch);
			VR_INPUT_REASON(calibration_invalid);
			VR_INPUT_REASON(pose_filter_rejected);
			VR_INPUT_REASON(action_value_invalid);
			VR_INPUT_REASON(reference_changed);
			VR_INPUT_REASON(view_configuration_unsupported);
			VR_INPUT_REASON(wait_frame_failed);
			VR_INPUT_REASON(begin_frame_failed);
			VR_INPUT_REASON(present_mismatch);
			VR_INPUT_REASON(frame_submission_failed);
#undef VR_INPUT_REASON
		default:
			return "unknown";
		}
	}
	inline const char* to_string(input_channel value) noexcept
	{
		switch (value)
		{
		case input_channel::focus:
			return "focus";
		case input_channel::move:
			return "move";
		case input_channel::turn:
			return "turn";
		case input_channel::left_grip:
			return "left_grip";
		case input_channel::right_grip:
			return "right_grip";
		case input_channel::left_aim:
			return "left_aim";
		case input_channel::right_aim:
			return "right_aim";
		case input_channel::sprint:
			return "sprint";
		case input_channel::jump:
			return "jump";
		case input_channel::left_trigger:
			return "left_trigger";
		case input_channel::right_trigger:
			return "right_trigger";
		case input_channel::left_squeeze:
			return "left_squeeze";
		case input_channel::right_squeeze:
			return "right_squeeze";
		case input_channel::count:
			break;
		}
		return "unknown";
	}
}
