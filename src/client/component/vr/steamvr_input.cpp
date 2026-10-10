#include <std_include.hpp>
#include "steamvr_input.hpp"
#include "controller_input.hpp"
#include "hud_controller.hpp"
#include "controller_haptics.hpp"
#include <filesystem>
#include <gsl/gsl>

namespace vr::steamvr_input
{
	bool actions::initialize(IVRSystem* system)
	{
		reset();
		pose_pipeline_ = controller_pose_pipeline::selected();
		system_ = system;
		++diagnostics_.initializations;
		setup_actions_ = {};
		setup_action_count_ = 0;
		manifest_code_ = set_code_ = -1;
		manifest_path_.clear();
		probe_count_ = 0;
		probe_pending_ = true;
		const auto record_setup = gsl::finally([this] {
			capture_setup();
			if (!input_) capture_probe(true);
		});
		std::array<wchar_t, 32768> executable{};
		const auto length =
		    GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
		if (length == 0 || length >= executable.size())
		{
			error_ = "cannot resolve executable path for SteamVR input manifest";
			controller_input::invalidate(controller_input::input_reason::initialization_failed,
			                             controller_input::input_backend::openvr, 0, diagnostics_.initializations);
			return false;
		}
		const auto path =
		    (std::filesystem::path(executable.data()).parent_path() / "vr_input" / "actions.json").u8string();
		manifest_path_.assign(reinterpret_cast<const char*>(path.c_str()));
		input_ = VRInput();
		if (!input_)
		{
			error_ = "IVRInput unavailable";
			controller_input::invalidate(controller_input::input_reason::not_initialized,
			                             controller_input::input_backend::openvr, 0, diagnostics_.initializations);
			return false;
		}
		auto error = input_->SetActionManifestPath(reinterpret_cast<const char*>(path.c_str()));
		manifest_code_ = error;
		if (error == VRInputError_None)
		{
			error = input_->GetActionSetHandle("/actions/gameplay", &set_);
			set_code_ = error;
		}
		const auto get = [&](const char* name, const char* type, VRActionHandle_t& handle, bool required = true)
		{
			action_setup action{name, type};
			if (error == VRInputError_None)
			{
				const auto result = input_->GetActionHandle(name, &handle);
				action.code = result;
				action.handle = handle;
				if (required) error = result;
				else if (result != VRInputError_None) handle = 0;
			}
			if (setup_action_count_ < setup_actions_.size()) setup_actions_[setup_action_count_++] = action;
			else ++diagnostics_.collection_failures;
		};
		get("/actions/gameplay/in/move", "vector2", move_);
		get("/actions/gameplay/in/turn", "vector2", turn_);
		get("/actions/gameplay/in/sprint", "boolean", sprint_);
		get("/actions/gameplay/in/jump", "boolean", jump_);
		get("/actions/gameplay/in/left_trigger", "boolean", trigger_[0]);
		get("/actions/gameplay/in/right_trigger", "boolean", trigger_[1]);
		get("/actions/gameplay/in/left_squeeze", "boolean", squeeze_[0]);
		get("/actions/gameplay/in/right_squeeze", "boolean", squeeze_[1]);
		get("/actions/gameplay/in/left_primary", "boolean", primary_[0]);
		get("/actions/gameplay/in/right_primary", "boolean", primary_[1]);
		get("/actions/gameplay/in/left_secondary", "boolean", secondary_[0]);
		get("/actions/gameplay/in/right_secondary", "boolean", secondary_[1]);
		get("/actions/gameplay/in/left_grip", "pose", grip_[0]);
		get("/actions/gameplay/in/right_grip", "pose", grip_[1]);
		get("/actions/gameplay/in/left_aim", "pose", aim_[0]);
		get("/actions/gameplay/in/right_aim", "pose", aim_[1]);
		if (error != VRInputError_None)
		{
			input_ = nullptr;
			initialization_code_ = error;
			error_ = "SteamVR input manifest/handle initialization failed: " + std::to_string(error);
			controller_input::invalidate(controller_input::input_reason::initialization_failed,
			                             controller_input::input_backend::openvr,
			                             error, diagnostics_.initializations);
			return false;
		}
		// Optional contact/output bindings must not disable otherwise working input.
		get("/actions/gameplay/in/menu_toggle", "boolean", menu_toggle_, false);
		get("/actions/gameplay/in/menu_recenter", "boolean", menu_recenter_, false);
		get("/actions/gameplay/in/left_trigger_touch", "boolean", trigger_touch_[0], false);
		get("/actions/gameplay/in/right_trigger_touch", "boolean", trigger_touch_[1], false);
		get("/actions/gameplay/out/left_haptic", "vibration", haptic_[0], false);
		get("/actions/gameplay/out/right_haptic", "vibration", haptic_[1], false);
		controller_haptics::bindings({haptic_[0] != 0, haptic_[1] != 0});
		return true;
	}

	bool actions::refresh_controller_type() noexcept
	{
		bool queried=true;
		bool knuckles=system_!=nullptr;
		for(const auto role:{TrackedControllerRole_LeftHand,TrackedControllerRole_RightHand})
		{
			if(!system_)break;
			const auto device=system_->GetTrackedDeviceIndexForControllerRole(role);
			if(device==k_unTrackedDeviceIndexInvalid || !system_->IsTrackedDeviceConnected(device))
			{knuckles=false;continue;}
			std::array<char,128> type{};
			ETrackedPropertyError error{};
			const auto bytes=system_->GetStringTrackedDeviceProperty(device,Prop_ControllerType_String,
				type.data(),static_cast<uint32_t>(type.size()),&error);
			const bool valid=error==TrackedProp_Success && bytes>0 && bytes<=type.size() && type[bytes-1]==0;
			queried=queried && valid;
			knuckles=knuckles && valid && std::string_view(type.data(),bytes-1)=="knuckles";
		}
		hud_controller::set_knuckles(controller_input::input_backend::openvr,knuckles);
		return queried;
	}

	void actions::sample(const bool focused, const controller_input::input_reason unavailable_reason) noexcept
	{
		using namespace controller_input;
		controller_input::frame frame{};
		frame.pose_pipeline = pose_pipeline_;
		frame.sequence = ++sequence_;
		frame.sampled_at = controller_input::clock::now();
		if(controller_type_retry_.ready(frame.sampled_at))
			controller_type_retry_.record_result(refresh_controller_type(),frame.sampled_at);
		frame.reference_generation = head_pose_bridge::get_status().recenter_count;
		frame.source.backend = input_backend::openvr;
		frame.source.initialization = diagnostics_.initializations;
		frame.source.runtime_focus = focused;
		frame.source.runtime_focus_known = unavailable_reason == input_reason::input_unavailable;
		VRActiveActionSet_t active{};
		active.ulActionSet = set_;
		const auto update =
		    input_ && focused ? input_->UpdateActionState(&active, sizeof(active), 1) : VRInputError_None;
		frame.source.gate = !input_ ? input_condition{error_.empty() ? input_reason::not_initialized
		                                                             : input_reason::initialization_failed,
		                                              initialization_code_}
		                    : !focused ? input_condition{unavailable_reason}
		                    : update != VRInputError_None
		                        ? input_condition{input_reason::action_update_failed, update, set_}
		                        : input_condition{input_reason::none};
		if (frame.source.gate.reason != input_reason::none)
		{
			if (frame.source.gate.reason == input_reason::action_update_failed && !sample_failed_)
			{
				sample_failed_ = true;
				capture_probe(true);
			}
			frame.sprint = sprint_state_.sample(false, false, frame.sampled_at);
			frame.jump = jump_state_.sample(false, false, frame.sampled_at);
			frame.menu_toggle = menu_toggle_state_.sample(false, false, frame.sampled_at);
			frame.menu_recenter = menu_recenter_state_.sample(false, false, frame.sampled_at);
			for (size_t hand = 0; hand < 2; ++hand)
				frame.trigger[hand] = trigger_state_[hand].sample(false, false, frame.sampled_at);
			for (size_t hand = 0; hand < 2; ++hand)
				frame.trigger_touch[hand] = trigger_touch_state_[hand].sample(false, false, frame.sampled_at);
			for (size_t hand = 0; hand < 2; ++hand)
				frame.squeeze[hand] = squeeze_state_[hand].sample(false, false, frame.sampled_at);
			for (size_t hand = 0; hand < 2; ++hand)
				frame.primary[hand] = primary_state_[hand].sample(false, false, frame.sampled_at);
			for (size_t hand = 0; hand < 2; ++hand)
				frame.secondary[hand] = secondary_state_[hand].sample(false, false, frame.sampled_at);
			controller_input::publish(frame);
			controller_haptics::clear();
			return;
		}
		frame.focused = true;
		const auto analog = [&](const VRActionHandle_t handle, auto& axis, input_channel channel)
		{
			InputAnalogActionData_t value{};
			const auto result =
			    input_->GetAnalogActionData(handle, &value, sizeof(value), k_ulInvalidInputValueHandle);
			frame.source.channels[index(channel)] =
			    result != VRInputError_None
			        ? input_condition{input_reason::action_query_failed, result, handle}
			        : input_condition{value.bActive ? input_reason::none : input_reason::action_inactive};
			if (result != VRInputError_None || !value.bActive)
				return false;
			axis = {value.x, value.y};
			return true;
		};
		frame.move_active = analog(move_, frame.move, input_channel::move);
		frame.turn_active = analog(turn_, frame.turn, input_channel::turn);
		const auto digital = [&](const VRActionHandle_t handle,
		                         controller_input::digital_sampler& state,
		                         input_channel channel = input_channel::count)
		{
			InputDigitalActionData_t value{};
			const auto result =
			    handle
			        ? input_->GetDigitalActionData(handle, &value, sizeof(value), k_ulInvalidInputValueHandle)
			        : VRInputError_None;
			const bool available = handle && result == VRInputError_None && value.bActive;
			if (channel != input_channel::count)
				frame.source.channels[index(channel)] =
				    result != VRInputError_None
				        ? input_condition{input_reason::action_query_failed, result, handle}
				        : input_condition{available ? input_reason::none : input_reason::action_inactive};
			return state.sample(available, value.bState, frame.sampled_at);
		};
		frame.sprint = digital(sprint_, sprint_state_, input_channel::sprint);
		frame.jump = digital(jump_, jump_state_, input_channel::jump);
		frame.menu_toggle = digital(menu_toggle_, menu_toggle_state_);
		frame.menu_recenter = digital(menu_recenter_, menu_recenter_state_);
		for (unsigned hand = 0; hand < 2; ++hand)
			frame.trigger[hand] = digital(
			    trigger_[hand], trigger_state_[hand], hand_channel(input_channel::left_trigger, hand));
		for (size_t hand = 0; hand < 2; ++hand)
			frame.trigger_touch[hand] = digital(trigger_touch_[hand], trigger_touch_state_[hand]);
		for (unsigned hand = 0; hand < 2; ++hand)
			frame.squeeze[hand] = digital(
			    squeeze_[hand], squeeze_state_[hand], hand_channel(input_channel::left_squeeze, hand));
		for (size_t hand = 0; hand < 2; ++hand)
			frame.primary[hand] = digital(primary_[hand], primary_state_[hand]);
		for (size_t hand = 0; hand < 2; ++hand)
			frame.secondary[hand] = digital(secondary_[hand], secondary_state_[hand]);
		const auto pose =
		    [&](const VRActionHandle_t handle, controller_input::hand_pose& output, input_channel channel, unsigned hand, bool aim)
		{
			InputPoseActionData_t value{};
			const auto result = input_->GetPoseActionDataForNextFrame(
			    handle, TrackingUniverseStanding, &value, sizeof(value), k_ulInvalidInputValueHandle);
			auto& condition = frame.source.channels[index(channel)];
			condition =
			    result != VRInputError_None      ? input_condition{input_reason::action_query_failed, result, handle}
			    : !value.bActive                 ? input_condition{input_reason::action_inactive}
			    : !value.pose.bDeviceIsConnected ? input_condition{input_reason::controller_disconnected}
			    : !value.pose.bPoseIsValid       ? input_condition{input_reason::pose_invalid}
			                                     : input_condition{input_reason::none};
			if (condition.reason != input_reason::none)
				return;
			const auto& matrix = value.pose.mDeviceToAbsoluteTracking.m;
			for (std::size_t row = 0; row < 3; ++row)
			{
				output.tracking.position_meters[row] = matrix[row][3];
				for (std::size_t col = 0; col < 3; ++col)
					output.tracking.orientation[row][col] = matrix[row][col];
			}
			output.valid = true;
			if (pose_pipeline_ == controller_pose_pipeline::mode::standard &&
				(!system_ || !pose_adapter_.normalize(*system_, *input_, VRRenderModels(), handle,
					value.activeOrigin, hand, aim, frame.sampled_at, output)))
			{
				output.valid = false;
				condition = {input_reason::pose_reference_unavailable, 0, handle};
			}
		};
		for (unsigned hand = 0; hand < 2; ++hand)
		{
			pose(grip_[hand], frame.grip[hand], hand_channel(input_channel::left_grip, hand), hand, false);
			pose(aim_[hand], frame.aim[hand], hand_channel(input_channel::left_aim, hand), hand, true);
		}
		const auto failed = std::any_of(frame.source.channels.begin(), frame.source.channels.end(),
		    [](const input_condition& condition) { return is_api_failure(condition.reason); });
		if (failed && !sample_failed_) probe_pending_ = true;
		sample_failed_ = failed;
		if (pose_pipeline_ == controller_pose_pipeline::mode::standard)
			frame.pose_reference_generation = pose_adapter_.generation();
		controller_input::publish(frame);
		if (probe_pending_) capture_probe(failed);
		const auto pulses = controller_haptics::consume(frame);
		for (size_t h = 0; h < pulses.size(); ++h)
		{
			const auto& p = pulses[h];
			if (haptic_[h] && p.amplitude > 0)
				controller_haptics::delivered(
				    static_cast<int>(h),
				    input_->TriggerHapticVibrationAction(
				        haptic_[h], 0, p.seconds, p.frequency, p.amplitude, k_ulInvalidInputValueHandle));
		}
	}

	void actions::reset() noexcept
	{
		hud_controller::set_knuckles(controller_input::input_backend::openvr,false);
		controller_type_retry_.reset();
		++diagnostics_.resets;
		probe_pending_ = false;
		sample_failed_ = false;
		system_ = nullptr;
		input_ = nullptr;
		set_ = move_ = turn_ = sprint_ = jump_ = 0;
		menu_toggle_ = 0;
		(void)menu_toggle_state_.sample(false, false, controller_input::clock::now());
		menu_recenter_ = 0;
		(void)menu_recenter_state_.sample(false, false, controller_input::clock::now());
		const auto now = controller_input::clock::now();
		(void)sprint_state_.sample(false, false, now);
		(void)jump_state_.sample(false, false, now);
		trigger_ = {};
		trigger_touch_ = {};
		squeeze_ = {};
		primary_ = {};
		secondary_ = {};
		for (auto& state : secondary_state_)
			(void)state.sample(false, false, now);
		for (auto& state : primary_state_)
			(void)state.sample(false, false, now);
		for (auto& state : squeeze_state_)
			(void)state.sample(false, false, now);
		for (auto& state : trigger_state_)
			(void)state.sample(false, false, now);
		for (auto& state : trigger_touch_state_)
			(void)state.sample(false, false, now);
		grip_ = aim_ = {};
		pose_adapter_.reset();
		haptic_ = {};
		controller_haptics::bindings({});
		controller_haptics::clear();
		error_.clear();
		initialization_code_ = 0;
		controller_input::invalidate(controller_input::input_reason::runtime_reset,
		                             controller_input::input_backend::openvr, 0, diagnostics_.initializations);
	}
}
