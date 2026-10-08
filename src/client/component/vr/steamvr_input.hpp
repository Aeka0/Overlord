#pragma once

#include <openvr.h>
#include <array>
#include <string>
#include "controller_input.hpp"
#include "steamvr_input_diagnostics.hpp"

namespace vr::steamvr_input
{
	// Used exclusively by the OpenVR runtime owner under its existing mutex.
	class actions
	{
	  public:
		bool initialize(IVRSystem* system);
		void sample(bool focused,
		            controller_input::input_reason unavailable_reason =
		                controller_input::input_reason::input_unavailable) noexcept;
		void reset() noexcept;
		void observe_event(const VREvent_t& event) noexcept;
		[[nodiscard]] const diagnostic_snapshot& diagnostics() const noexcept { return diagnostics_; }
		[[nodiscard]] const std::string& error() const noexcept
		{
			return error_;
		}

	  private:
		IVRInput* input_{};
		IVRSystem* system_{}; // Borrowed from the same runtime owner.
		struct action_setup
		{
			const char* name{};
			const char* type{};
			VRActionHandle_t handle{};
			int code{-1};
		};
		std::array<action_setup, 22> setup_actions_{};
		std::size_t setup_action_count_{};
		int manifest_code_{-1}, set_code_{-1};
		std::string manifest_path_;
		diagnostic_snapshot diagnostics_;
		unsigned probe_count_{};
		bool probe_pending_{}, sample_failed_{};
		void capture_setup() noexcept;
		void capture_probe(bool input_failed = false) noexcept;
		VRActionSetHandle_t set_{};
		VRActionHandle_t move_{};
		VRActionHandle_t turn_{};
		VRActionHandle_t sprint_{};
		VRActionHandle_t jump_{};
		VRActionHandle_t menu_toggle_{};
		controller_input::digital_sampler menu_toggle_state_;
		VRActionHandle_t menu_recenter_{};
		controller_input::digital_sampler menu_recenter_state_;
		controller_input::digital_sampler sprint_state_;
		controller_input::digital_sampler jump_state_;
		std::array<VRActionHandle_t, 2> trigger_{};
		std::array<controller_input::digital_sampler, 2> trigger_state_;
		std::array<VRActionHandle_t, 2> trigger_touch_{};
		std::array<controller_input::digital_sampler, 2> trigger_touch_state_;
		std::array<VRActionHandle_t, 2> squeeze_{};
		std::array<controller_input::digital_sampler, 2> squeeze_state_;
		std::array<VRActionHandle_t, 2> primary_{};
		std::array<controller_input::digital_sampler, 2> primary_state_;
		std::array<VRActionHandle_t, 2> secondary_{};
		std::array<controller_input::digital_sampler, 2> secondary_state_;
		std::array<VRActionHandle_t, 2> grip_{};
		std::array<VRActionHandle_t, 2> aim_{};
		std::array<VRActionHandle_t, 2> haptic_{};
		std::uint64_t sequence_{};
		std::string error_;
		std::int64_t initialization_code_{};
	};
}
