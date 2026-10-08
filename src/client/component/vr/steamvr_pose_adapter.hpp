#pragma once

#include "controller_pose_reference.hpp"
#include <openvr.h>
#include <string_view>

namespace vr::steamvr_input
{
	// Standard pipeline only. Cache static binding/model transforms, not poses or
	// device handles. Binding/device events and changing active origins retire it.
	class pose_adapter
	{
		struct entry
		{
			VRInputValueHandle_t origin{};
			controller_input::clock::time_point retry_after{};
			pose_filter::pose source_from_standard;
			bool ready{};
		};
		std::array<entry, 4> entries_{};
		std::uint64_t generation_{1};
	public:
		void reset() noexcept { entries_ = {}; ++generation_; }
		std::uint64_t generation() const noexcept { return generation_; }
		template <class System, class Input, class Models>
		bool normalize(System& system, Input& input, Models* models,
		    VRActionHandle_t action, VRInputValueHandle_t origin, unsigned hand, bool aim,
		    controller_input::clock::time_point now, controller_input::hand_pose& pose) noexcept;
	};
}

namespace vr::steamvr_input
{
	namespace pose_adapter_detail
	{
		template <std::size_t N> std::string_view bounded(const char (&value)[N]) noexcept
		{
			const auto end = std::find(value, value + N, '\0');
			return end == value + N ? std::string_view{} : std::string_view(value, end - value);
		}
		template <class Models> bool component(Models& models, const char* model, const char* name,
		    pose_filter::pose& result) noexcept
		{
			VRControllerState_t neutral{};
			RenderModel_ControllerMode_State_t mode{};
			RenderModel_ComponentState_t value{};
			if (!models.GetComponentState(model, name, &neutral, &mode, &value) ||
			    !(value.uProperties & VRComponentProperty_IsStatic)) return false;
			for (unsigned row = 0; row < 3; ++row)
			{
				result.position[row] = value.mTrackingToComponentLocal.m[row][3];
				for (unsigned column = 0; column < 3; ++column)
					result.orientation[row][column] = value.mTrackingToComponentLocal.m[row][column];
			}
			return controller_pose_reference::valid_component(result);
		}
		template <class System, class Input, class Models>
		bool resolve(System& system, Input& input, Models* models,
		    VRActionHandle_t action, VRInputValueHandle_t origin, unsigned hand, bool aim,
		    pose_filter::pose& source_from_standard) noexcept
		{
			if (!models || !origin) return false;
			InputOriginInfo_t info{};
			if (input.GetOriginTrackedDeviceInfo(origin, &info, sizeof(info)) != VRInputError_None ||
			    info.trackedDeviceIndex >= k_unMaxTrackedDeviceCount ||
			    system.GetTrackedDeviceClass(info.trackedDeviceIndex) != TrackedDeviceClass_Controller ||
			    !system.IsTrackedDeviceConnected(info.trackedDeviceIndex) ||
			    system.GetControllerRoleForTrackedDeviceIndex(info.trackedDeviceIndex) !=
			        (hand == 0 ? TrackedControllerRole_LeftHand : TrackedControllerRole_RightHand)) return false;
			// A user binding need not still point at /pose/raw or /pose/tip.
			std::array<InputBindingInfo_t, 8> bindings{};
			uint32_t count{};
			if (input.GetActionBindingInfo(action, bindings.data(), sizeof(InputBindingInfo_t),
			    unsigned(bindings.size()), &count) != VRInputError_None || count != 1) return false;
			const auto device = bounded(bindings[0].rchDevicePathName);
			if (device != (hand == 0 ? "/user/hand/left" : "/user/hand/right")) return false;
			auto path = bounded(bindings[0].rchInputPathName);
			if (path.starts_with(device)) path.remove_prefix(device.size());
			if (!path.starts_with("/pose/")) return false;
			path.remove_prefix(6);
			if (path.empty() || path.size() >= 128 || path.find('/') != path.npos) return false;
			std::array<char, 128> source_name{};
			std::copy(path.begin(), path.end(), source_name.begin());
			std::array<char, 512> model{};
			ETrackedPropertyError error{};
			const auto size = system.GetStringTrackedDeviceProperty(info.trackedDeviceIndex,
			    Prop_RenderModelName_String, model.data(), unsigned(model.size()), &error);
			if (error != TrackedProp_Success || size < 2 || size > model.size() || model[size - 1] != 0) return false;
			pose_filter::pose device_from_source, device_from_standard;
			if (path != "raw" && !component(*models, model.data(), source_name.data(), device_from_source)) return false;
			if (!component(*models, model.data(), aim ? k_pch_Controller_Component_OpenXR_Aim
			    : k_pch_Controller_Component_OpenXR_Grip, device_from_standard)) return false;
			source_from_standard = pose_filter::compose(pose_filter::inverse(device_from_source), device_from_standard);
			return controller_pose_reference::valid_component(source_from_standard);
		}
	}

	template <class System, class Input, class Models>
	bool pose_adapter::normalize(System& system, Input& input, Models* models,
	    VRActionHandle_t action, VRInputValueHandle_t origin, unsigned hand, bool aim,
	    controller_input::clock::time_point now, controller_input::hand_pose& pose) noexcept
	{
		if (hand >= 2 || !pose.valid) return false;
		auto& cached = entries_[hand * 2 + unsigned(aim)];
		if (cached.origin != origin) { cached = {.origin = origin}; ++generation_; }
		if (!cached.ready && now >= cached.retry_after)
		{
			cached.ready = pose_adapter_detail::resolve(system, input, models, action, origin, hand, aim, cached.source_from_standard);
			// Models/bindings can arrive after tracking. Successful samples do no
			// metadata lookup; missing metadata retries at most once per second.
			cached.retry_after = now + std::chrono::seconds(1);
		}
		if (!cached.ready) { pose.valid = false; return false; }
		const auto converted = pose_filter::compose(
		    {pose.tracking.position_meters, pose.tracking.orientation}, cached.source_from_standard);
		pose.valid = pose_filter::valid(converted);
		if (pose.valid) pose.tracking = {converted.position, converted.orientation};
		return pose.valid;
	}
}
