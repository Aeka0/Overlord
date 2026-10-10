#pragma once
#include "openxr_dispatch.hpp"
#include "controller_input.hpp"
#include "controller_pose_reference.hpp"
#include "controller_profile_retry.hpp"
#include <vector>

#if H2V_OPENXR_HEADERS_AVAILABLE
namespace vr::openxr
{
	// Session-owner input. Native gameplay consumes the existing copied frame;
	// it never queries actions/spaces or decides which runtime owns a hand.
	class input_actions
	{
		struct hand_actions
		{
			XrAction trigger{}, squeeze{}, touch{}, primary{}, secondary{}, grip{}, aim{}, haptic{};
			XrSpace grip_space{}, aim_space{};
			controller_input::digital_sampler trigger_state, squeeze_state, touch_state, primary_state,
			    secondary_state;
			controller_input::analog_latch trigger_latch, squeeze_latch;
			controller_input::analog_policy trigger_policy{}, squeeze_policy{};
		};
		XrActionSet set_{};
		XrAction move_{}, turn_{}, sprint_{}, jump_{}, menu_{}, recenter_{};
		std::array<hand_actions, 2> hands_;
		controller_input::digital_sampler sprint_state_, jump_state_, menu_state_, recenter_state_;
		std::uint64_t sequence_{};
		controller_pose_pipeline::mode pose_pipeline_{controller_pose_pipeline::mode::legacy};
		controller_pose_reference::configuration grip_reference_;
		XrInstance instance_{};
		bool profile_refresh_pending_{true};
		controller_profile::refresh_retry prompt_profile_retry_;
		std::array<controller_profile::refresh_retry, 2> analog_profile_retry_;
		std::vector<XrPath> binding_profiles_;
		void refresh_analog_profiles(const dispatch_table&, XrSession, controller_input::clock::time_point) noexcept;
		bool refresh_prompt_profile(const dispatch_table&,XrSession) noexcept;
		std::array<bool, 2> profile_matches_{};
		bool refresh_profiles(const dispatch_table&, XrSession, XrResult&, std::string&);

	  public:
		void set_grip_reference(controller_pose_reference::configuration reference,
			controller_pose_pipeline::mode mode = controller_pose_pipeline::mode::legacy);
		void profile_changed() noexcept;
		const controller_pose_reference::configuration& grip_reference() const noexcept
		{
			return grip_reference_;
		}
		bool initialize(const dispatch_table&, XrInstance, XrSession, XrResult&, std::string&);
		// Returns false for API failure; unfocused input publishes a neutral frame.
		bool sample(const dispatch_table&, XrSession, XrSpace, XrTime, bool focused, XrResult&, std::string&);
		void invalidate(controller_input::input_reason reason = controller_input::input_reason::runtime_reset,
		                std::int64_t code = 0) noexcept;
		// Retains failed destroy handles for the existing teardown retry contract.
		call_result destroy(const dispatch_table&) noexcept;
	};
}
#endif
