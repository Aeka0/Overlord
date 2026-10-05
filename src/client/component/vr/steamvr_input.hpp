#pragma once

#include <openvr.h>
#include <array>
#include <string>
#include "controller_input.hpp"

namespace vr::steamvr_input
{
	// Used exclusively by the OpenVR runtime owner under its existing mutex.
	class actions
	{
	public:
		bool initialize();
		void sample(bool focused) noexcept;
		void reset() noexcept;
		[[nodiscard]] const std::string& error() const noexcept { return error_; }

	private:
		IVRInput* input_{};
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
	};
}
