#pragma once

#include "component/vr/steamvr_pose_adapter.hpp"
#include "component/vr/touch_controller_reference.hpp"
#include <cstring>

namespace controller_pose_pipeline_tests
{
	using namespace vr;
	struct system
	{
		bool connected{true};
		ETrackedDeviceClass GetTrackedDeviceClass(TrackedDeviceIndex_t) { return TrackedDeviceClass_Controller; }
		bool IsTrackedDeviceConnected(TrackedDeviceIndex_t) { return connected; }
		ETrackedControllerRole GetControllerRoleForTrackedDeviceIndex(TrackedDeviceIndex_t i)
		{ return i == 1 ? TrackedControllerRole_LeftHand : TrackedControllerRole_RightHand; }
		uint32_t GetStringTrackedDeviceProperty(TrackedDeviceIndex_t, ETrackedDeviceProperty, char* value,
		    uint32_t, ETrackedPropertyError* error)
		{ std::strcpy(value, "fixture"); *error = TrackedProp_Success; return 8; }
	};
	struct input
	{
		unsigned queries{}, count{1};
		bool wrong_hand{}, truncated{};
		const char* source{"/pose/raw"};
		EVRInputError GetOriginTrackedDeviceInfo(VRInputValueHandle_t origin, InputOriginInfo_t* value, uint32_t)
		{ ++queries; value->trackedDeviceIndex = unsigned(origin); return VRInputError_None; }
		EVRInputError GetActionBindingInfo(VRActionHandle_t hand, InputBindingInfo_t* value, uint32_t, uint32_t, uint32_t* n)
		{
			*n = count;
			std::strcpy(value->rchDevicePathName, hand == 1 && !wrong_hand ? "/user/hand/left" : "/user/hand/right");
			std::strcpy(value->rchInputPathName, source);
			if (truncated) std::memset(value->rchInputPathName, 'x', sizeof(value->rchInputPathName));
			return VRInputError_None;
		}
	};
	struct models
	{
		bool available{true}, rigid{true};
		pose_filter::pose grip{{.01f, -.02f, .1f}, controller_calibration::rotation({20, 3, 0})};
		pose_filter::pose tip{{0, 0, -.04f}, controller_calibration::rotation({-8, 0, 0})};
		bool GetComponentState(const char*, const char* name, const VRControllerState_t*,
		    const RenderModel_ControllerMode_State_t*, RenderModel_ComponentState_t* value)
		{
			if (!available) return false;
			const auto& p = std::string_view(name) == "openxr_grip" ? grip : tip;
			value->uProperties = rigid ? VRComponentProperty_IsStatic : 0;
			for (unsigned r = 0; r < 3; ++r)
			{
				value->mTrackingToComponentLocal.m[r][3] = p.position[r];
				for (unsigned c = 0; c < 3; ++c) value->mTrackingToComponentLocal.m[r][c] = p.orientation[r][c];
			}
			return true;
		}
	};
	template <class Check> void run(Check check)
	{
		using namespace std::chrono_literals;
		system system;
		input input;
		models models;
		steamvr_input::pose_adapter adapter;
		const auto now = controller_input::clock::time_point{} + 1s;
		const pose_filter::pose device{{.3f, 1.2f, -.6f}, controller_calibration::rotation({45, -30, 10})};
		const auto hand_pose = [](const pose_filter::pose& p) { return controller_input::hand_pose{true, {p.position, p.orientation}}; };
		auto pose = hand_pose(device);
		const auto expected = pose_filter::compose(device, models.grip);
		check(adapter.normalize(system, input, &models, 1, 1, 0, false, now, pose) &&
		    pose_filter::length(pose_filter::sub(pose.tracking.position_meters, expected.position)) < 1e-6f,
		    "OpenVR raw device pose becomes the same standard grip as OpenXR");
		pose = hand_pose(device);
		check(adapter.normalize(system, input, &models, 1, 1, 0, false, now + 1ms, pose) && input.queries == 1,
		    "Successful OpenVR conversion does not poll metadata per frame");
		adapter.reset();
		input.source = "/user/hand/left/pose/openxr_grip";
		pose = hand_pose(expected);
		check(adapter.normalize(system, input, &models, 1, 1, 0, false, now, pose) &&
		    pose_filter::length(pose_filter::sub(pose.tracking.position_meters, expected.position)) < 1e-6f,
		    "Rebinding to an already standard grip is not double converted");
		adapter.reset(); input.source = "/pose/tip";
		const auto aim = pose_filter::compose(device, models.tip);
		pose = hand_pose(aim);
		check(adapter.normalize(system, input, &models, 1, 1, 0, true, now, pose) &&
		    pose_filter::length(pose_filter::sub(pose.tracking.position_meters, aim.position)) < 1e-6f,
		    "Canonical aim preserves the pointing witness");
		for (unsigned fault = 0; fault < 5; ++fault)
		{
			adapter.reset(); input = {}; models = {}; system = {};
			if (fault == 0) input.count = 9;
			if (fault == 1) input.wrong_hand = true;
			if (fault == 2) input.truncated = true;
			if (fault == 3) models.rigid = false;
			if (fault == 4) system.connected = false;
			pose = hand_pose(device);
			check(!adapter.normalize(system, input, &models, 1, 1, 0, false, now, pose) && !pose.valid,
			    "Ambiguous bindings, wrong hands, malformed paths, moving components and disconnects fail closed");
		}
		adapter.reset(); input = {}; models = {}; system = {}; models.available = false;
		pose = hand_pose(device);
		check(!adapter.normalize(system, input, &models, 1, 1, 0, false, now, pose), "Missing model cannot fabricate a pose");
		models.available = true; pose = hand_pose(device);
		check(!adapter.normalize(system, input, &models, 1, 1, 0, false, now + 1ms, pose) && input.queries == 1,
		    "Delayed metadata retry is bounded");
		pose = hand_pose(device);
		check(adapter.normalize(system, input, &models, 1, 1, 0, false, now + 1s, pose), "Late models recover without restarting");
		adapter.reset(); input.wrong_hand = true; pose = hand_pose(device);
		check(!adapter.normalize(system, input, &models, 1, 1, 0, false, now, pose), "Binding events retire successful cached transforms");
		controller_input::frame before, after;
		before.pose_reference_generation = adapter.generation();
		after = before;
		adapter.reset();
		after.pose_reference_generation = adapter.generation();
		check(controller_input::producer_discontinuity(before, after), "Rebinding fences physical velocity history even if tracking remains valid");
		// The converted application baseline preserves the previous physical wrist
		// point while the published grip itself remains in standard coordinates.
		const auto legacy = controller_pose_reference::touch_legacy_reference();
		for (unsigned hand = 0; hand < 2; ++hand)
		{
			const auto sign = hand == 0 ? 1.f : -1.f;
			const pose_filter::vec old_lever{sign * settings::wrist_inward.default_value,
			    settings::wrist_up.default_value, settings::wrist_back.default_value};
			const auto canonical = pose_filter::compose(legacy.hands[hand].grip_from_calibration, {old_lever});
			const pose_filter::vec baseline{sign * settings::standard_wrist_pivots[0].default_value,
			    settings::standard_wrist_pivots[2].default_value, settings::standard_wrist_pivots[1].default_value};
			check(pose_filter::length(pose_filter::sub(canonical.position, baseline)) < 1e-6f,
			    "Standard wrist baseline retains the original physical point on both hands");
		}
	}
}
