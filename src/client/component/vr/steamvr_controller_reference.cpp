#include <std_include.hpp>
#include "steamvr_controller_reference.hpp"
#include <openvr.h>
#include <array>

namespace vr::steamvr
{
	namespace
	{
		pose_filter::pose copy_component(const HmdMatrix34_t& matrix) noexcept
		{
			pose_filter::pose result;
			for (unsigned row = 0; row < 3; ++row)
			{
				result.position[row] = matrix.m[row][3];
				for (unsigned column = 0; column < 3; ++column)
					result.orientation[row][column] = matrix.m[row][column];
			}
			return result;
		}
		TrackedDeviceIndex_t controller_for_hand(IVRSystem& system, ETrackedControllerRole role)
		{
			const auto assigned = system.GetTrackedDeviceIndexForControllerRole(role);
			if (assigned != k_unTrackedDeviceIndexInvalid)
				return assigned;
			// Sleeping controllers may retain valid static model/role properties.
			// Those identify the device; current XR pose validity still owns readiness.
			TrackedDeviceIndex_t candidate = k_unTrackedDeviceIndexInvalid;
			for (TrackedDeviceIndex_t index = 0; index < k_unMaxTrackedDeviceCount; ++index)
			{
				if (system.GetTrackedDeviceClass(index) != TrackedDeviceClass_Controller)
					continue;
				ETrackedPropertyError error{};
				const auto hint =
				    system.GetInt32TrackedDeviceProperty(index, Prop_ControllerRoleHint_Int32, &error);
				if (error != TrackedProp_Success || hint != role)
					continue;
				if (candidate != k_unTrackedDeviceIndexInvalid)
					return k_unTrackedDeviceIndexInvalid;
				candidate = index;
			}
			return candidate;
		}
	}

	openxr_metadata query_openxr_metadata(bool steamvr_selected)
	{
		openxr_metadata metadata;
		auto& reference = metadata.reference;
		reference.target = controller_pose_reference::basis::calibration_frame;
		reference.expected_runtime = "SteamVR/OpenXR";
		reference.name = "steamvr_device_origin";
		EVRInitError initialization{};
		auto* system = VR_Init(&initialization, VRApplication_Background);
		const auto shutdown = gsl::finally([] { VR_Shutdown(); });
		if (!system)
		{
			reference.error = std::format("SteamVR controller metadata unavailable: {}",
			                              VR_GetVRInitErrorAsEnglishDescription(initialization));
			return metadata;
		}
		metadata.hmd_connected = system->GetTrackedDeviceClass(k_unTrackedDeviceIndex_Hmd) == TrackedDeviceClass_HMD &&
		                         system->IsTrackedDeviceConnected(k_unTrackedDeviceIndex_Hmd);
		std::array<char, 256> driver{};
		ETrackedPropertyError property_error{};
		const auto driver_size = system->GetStringTrackedDeviceProperty(k_unTrackedDeviceIndex_Hmd,
		    Prop_ActualTrackingSystemName_String, driver.data(), unsigned(driver.size()), &property_error);
		if (property_error == TrackedProp_Success && driver_size > 1 && driver_size <= driver.size())
			metadata.hmd_driver.assign(driver.data(), driver_size - 1);
		const auto remote_client = system->GetUint64TrackedDeviceProperty(k_unTrackedDeviceIndex_Hmd,
		    Prop_SteamRemoteClientID_Uint64, &property_error);
		metadata.remote_client_id_error = static_cast<std::int32_t>(property_error);
		if (property_error == TrackedProp_Success)
			metadata.remote_client_id = remote_client;
		if (!steamvr_selected && !metadata.connected_steam_link())
			return metadata;
		auto* models = VRRenderModels();
		if (!models)
		{
			reference.error = "SteamVR render-model metadata interface unavailable";
			return metadata;
		}
		for (unsigned hand = 0; hand < reference.hands.size(); ++hand)
		{
			auto& output = reference.hands[hand];
			const auto index = controller_for_hand(
			    *system, hand == 0 ? TrackedControllerRole_LeftHand : TrackedControllerRole_RightHand);
			std::array<char, 512> name{};
			ETrackedPropertyError error{};
			const auto size = system->GetStringTrackedDeviceProperty(
			    index, Prop_RenderModelName_String, name.data(), unsigned(name.size()), &error);
			if (error != TrackedProp_Success || size < 2 || size > name.size())
				continue;
			output.reference_id.assign(name.data(), size - 1);
			RenderModel_ComponentState_t component{};
			VRControllerState_t neutral{};
			RenderModel_ControllerMode_State_t mode{};
			// This overload permits querying STATIC geometry without an Action origin
			// or an input manifest. Never accept a state-dependent component here.
			if (!models->GetComponentState(output.reference_id.c_str(),
			                               k_pch_Controller_Component_OpenXR_Grip,
			                               &neutral,
			                               &mode,
			                               &component) ||
			    !(component.uProperties & VRComponentProperty_IsStatic))
				continue;
			const auto device_from_grip = copy_component(component.mTrackingToComponentLocal);
			if (!controller_pose_reference::valid_component(device_from_grip))
				continue;
			output.grip_from_calibration = pose_filter::inverse(device_from_grip);
			output.ready = true;
		}
		if (!reference.hands[0].ready || !reference.hands[1].ready)
			reference.error =
			    "SteamVR static openxr_grip reference missing or ambiguous; affected gameplay hand is unavailable; use vr_reinit after controller identification";
		return metadata;
	}
}
