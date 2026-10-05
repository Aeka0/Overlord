#pragma once

#include <openvr.h>
#include <stdexcept>

// Strict offline doubles for the vendored OpenVR interfaces. Any unexpected
// SDK call fails instead of contacting SteamVR or silently succeeding.
namespace vr::tests
{
	class IVRCompositor_stub : public IVRCompositor
	{
	public:
		void SetTrackingSpace(ETrackingUniverseOrigin) override
		{ throw std::logic_error("unexpected IVRCompositor::SetTrackingSpace"); }
		ETrackingUniverseOrigin GetTrackingSpace() override
		{ throw std::logic_error("unexpected IVRCompositor::GetTrackingSpace"); }
		EVRCompositorError WaitGetPoses(TrackedDevicePose_t*, uint32_t, TrackedDevicePose_t*, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::WaitGetPoses"); }
		EVRCompositorError GetLastPoses(TrackedDevicePose_t*, uint32_t, TrackedDevicePose_t*, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::GetLastPoses"); }
		EVRCompositorError GetLastPoseForTrackedDeviceIndex(TrackedDeviceIndex_t, TrackedDevicePose_t *, TrackedDevicePose_t *) override
		{ throw std::logic_error("unexpected IVRCompositor::GetLastPoseForTrackedDeviceIndex"); }
		EVRCompositorError GetSubmitTexture(Texture_t *, bool *, EVRCompositorTextureUsage, const Texture_t *, const VRTextureBounds_t *, EVRSubmitFlags) override
		{ throw std::logic_error("unexpected IVRCompositor::GetSubmitTexture"); }
		EVRCompositorError Submit(EVREye, const Texture_t *, const VRTextureBounds_t*, EVRSubmitFlags) override
		{ throw std::logic_error("unexpected IVRCompositor::Submit"); }
		EVRCompositorError SubmitWithArrayIndex(EVREye, const Texture_t *, uint32_t, const VRTextureBounds_t *, EVRSubmitFlags) override
		{ throw std::logic_error("unexpected IVRCompositor::SubmitWithArrayIndex"); }
		void ClearLastSubmittedFrame() override
		{ throw std::logic_error("unexpected IVRCompositor::ClearLastSubmittedFrame"); }
		void PostPresentHandoff() override
		{ throw std::logic_error("unexpected IVRCompositor::PostPresentHandoff"); }
		bool GetFrameTiming(Compositor_FrameTiming *, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::GetFrameTiming"); }
		uint32_t GetFrameTimings(Compositor_FrameTiming *, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::GetFrameTimings"); }
		float GetFrameTimeRemaining() override
		{ throw std::logic_error("unexpected IVRCompositor::GetFrameTimeRemaining"); }
		void GetCumulativeStats(Compositor_CumulativeStats *, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::GetCumulativeStats"); }
		void FadeToColor(float, float, float, float, float, bool) override
		{ throw std::logic_error("unexpected IVRCompositor::FadeToColor"); }
		HmdColor_t GetCurrentFadeColor(bool) override
		{ throw std::logic_error("unexpected IVRCompositor::GetCurrentFadeColor"); }
		void FadeGrid(float, bool) override
		{ throw std::logic_error("unexpected IVRCompositor::FadeGrid"); }
		float GetCurrentGridAlpha() override
		{ throw std::logic_error("unexpected IVRCompositor::GetCurrentGridAlpha"); }
		EVRCompositorError SetSkyboxOverride(const Texture_t *, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::SetSkyboxOverride"); }
		void ClearSkyboxOverride() override
		{ throw std::logic_error("unexpected IVRCompositor::ClearSkyboxOverride"); }
		void CompositorBringToFront() override
		{ throw std::logic_error("unexpected IVRCompositor::CompositorBringToFront"); }
		void CompositorGoToBack() override
		{ throw std::logic_error("unexpected IVRCompositor::CompositorGoToBack"); }
		void CompositorQuit() override
		{ throw std::logic_error("unexpected IVRCompositor::CompositorQuit"); }
		bool IsFullscreen() override
		{ throw std::logic_error("unexpected IVRCompositor::IsFullscreen"); }
		uint32_t GetCurrentSceneFocusProcess() override
		{ throw std::logic_error("unexpected IVRCompositor::GetCurrentSceneFocusProcess"); }
		uint32_t GetLastFrameRenderer() override
		{ throw std::logic_error("unexpected IVRCompositor::GetLastFrameRenderer"); }
		bool CanRenderScene() override
		{ throw std::logic_error("unexpected IVRCompositor::CanRenderScene"); }
		void ShowMirrorWindow() override
		{ throw std::logic_error("unexpected IVRCompositor::ShowMirrorWindow"); }
		void HideMirrorWindow() override
		{ throw std::logic_error("unexpected IVRCompositor::HideMirrorWindow"); }
		bool IsMirrorWindowVisible() override
		{ throw std::logic_error("unexpected IVRCompositor::IsMirrorWindowVisible"); }
		void CompositorDumpImages() override
		{ throw std::logic_error("unexpected IVRCompositor::CompositorDumpImages"); }
		bool ShouldAppRenderWithLowResources() override
		{ throw std::logic_error("unexpected IVRCompositor::ShouldAppRenderWithLowResources"); }
		void ForceInterleavedReprojectionOn(bool) override
		{ throw std::logic_error("unexpected IVRCompositor::ForceInterleavedReprojectionOn"); }
		void ForceReconnectProcess() override
		{ throw std::logic_error("unexpected IVRCompositor::ForceReconnectProcess"); }
		void SuspendRendering(bool) override
		{ throw std::logic_error("unexpected IVRCompositor::SuspendRendering"); }
		vr::EVRCompositorError GetMirrorTextureD3D11(vr::EVREye, void *, void **) override
		{ throw std::logic_error("unexpected IVRCompositor::GetMirrorTextureD3D11"); }
		void ReleaseMirrorTextureD3D11(void *) override
		{ throw std::logic_error("unexpected IVRCompositor::ReleaseMirrorTextureD3D11"); }
		vr::EVRCompositorError GetMirrorTextureGL(vr::EVREye, vr::glUInt_t *, vr::glSharedTextureHandle_t *) override
		{ throw std::logic_error("unexpected IVRCompositor::GetMirrorTextureGL"); }
		bool ReleaseSharedGLTexture(vr::glUInt_t, vr::glSharedTextureHandle_t) override
		{ throw std::logic_error("unexpected IVRCompositor::ReleaseSharedGLTexture"); }
		void LockGLSharedTextureForAccess(vr::glSharedTextureHandle_t) override
		{ throw std::logic_error("unexpected IVRCompositor::LockGLSharedTextureForAccess"); }
		void UnlockGLSharedTextureForAccess(vr::glSharedTextureHandle_t) override
		{ throw std::logic_error("unexpected IVRCompositor::UnlockGLSharedTextureForAccess"); }
		uint32_t GetVulkanInstanceExtensionsRequired(char *, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::GetVulkanInstanceExtensionsRequired"); }
		uint32_t GetVulkanDeviceExtensionsRequired(VkPhysicalDevice_T *, char *, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::GetVulkanDeviceExtensionsRequired"); }
		void SetExplicitTimingMode(EVRCompositorTimingMode) override
		{ throw std::logic_error("unexpected IVRCompositor::SetExplicitTimingMode"); }
		EVRCompositorError SubmitExplicitTimingData() override
		{ throw std::logic_error("unexpected IVRCompositor::SubmitExplicitTimingData"); }
		bool IsMotionSmoothingEnabled() override
		{ throw std::logic_error("unexpected IVRCompositor::IsMotionSmoothingEnabled"); }
		bool IsMotionSmoothingSupported() override
		{ throw std::logic_error("unexpected IVRCompositor::IsMotionSmoothingSupported"); }
		bool IsCurrentSceneFocusAppLoading() override
		{ throw std::logic_error("unexpected IVRCompositor::IsCurrentSceneFocusAppLoading"); }
		EVRCompositorError SetStageOverride_Async(const char *, const HmdMatrix34_t *, const Compositor_StageRenderSettings *, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::SetStageOverride_Async"); }
		void ClearStageOverride() override
		{ throw std::logic_error("unexpected IVRCompositor::ClearStageOverride"); }
		bool GetCompositorBenchmarkResults(Compositor_BenchmarkResults *, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::GetCompositorBenchmarkResults"); }
		EVRCompositorError GetLastPosePredictionIDs(uint32_t *, uint32_t *) override
		{ throw std::logic_error("unexpected IVRCompositor::GetLastPosePredictionIDs"); }
		EVRCompositorError GetPosesForFrame(uint32_t, TrackedDevicePose_t*, uint32_t) override
		{ throw std::logic_error("unexpected IVRCompositor::GetPosesForFrame"); }
	};

	class IVRSystem_stub : public IVRSystem
	{
	public:
		void GetRecommendedRenderTargetSize(uint32_t *, uint32_t *) override
		{ throw std::logic_error("unexpected IVRSystem::GetRecommendedRenderTargetSize"); }
		HmdMatrix44_t GetProjectionMatrix(EVREye, float, float) override
		{ throw std::logic_error("unexpected IVRSystem::GetProjectionMatrix"); }
		void GetProjectionRaw(EVREye, float *, float *, float *, float *) override
		{ throw std::logic_error("unexpected IVRSystem::GetProjectionRaw"); }
		bool ComputeDistortion(EVREye, float, float, DistortionCoordinates_t *) override
		{ throw std::logic_error("unexpected IVRSystem::ComputeDistortion"); }
		bool ComputeDistortionSet(EVREye, EVRDistortionChannel, bool, uint32_t, const DistortionCoordinate_t *, DistortionCoordinate_t *) override
		{ throw std::logic_error("unexpected IVRSystem::ComputeDistortionSet"); }
		HmdMatrix34_t GetEyeToHeadTransform(EVREye) override
		{ throw std::logic_error("unexpected IVRSystem::GetEyeToHeadTransform"); }
		bool GetTimeSinceLastVsync(float *, uint64_t *) override
		{ throw std::logic_error("unexpected IVRSystem::GetTimeSinceLastVsync"); }
		int32_t GetD3D9AdapterIndex() override
		{ throw std::logic_error("unexpected IVRSystem::GetD3D9AdapterIndex"); }
		void GetDXGIOutputInfo(int32_t *) override
		{ throw std::logic_error("unexpected IVRSystem::GetDXGIOutputInfo"); }
		void GetOutputDevice(uint64_t *, ETextureType, VkInstance_T *) override
		{ throw std::logic_error("unexpected IVRSystem::GetOutputDevice"); }
		bool IsDisplayOnDesktop() override
		{ throw std::logic_error("unexpected IVRSystem::IsDisplayOnDesktop"); }
		bool SetDisplayVisibility(bool) override
		{ throw std::logic_error("unexpected IVRSystem::SetDisplayVisibility"); }
		void GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin, float, TrackedDevicePose_t *, uint32_t) override
		{ throw std::logic_error("unexpected IVRSystem::GetDeviceToAbsoluteTrackingPose"); }
		HmdMatrix34_t GetSeatedZeroPoseToStandingAbsoluteTrackingPose() override
		{ throw std::logic_error("unexpected IVRSystem::GetSeatedZeroPoseToStandingAbsoluteTrackingPose"); }
		HmdMatrix34_t GetRawZeroPoseToStandingAbsoluteTrackingPose() override
		{ throw std::logic_error("unexpected IVRSystem::GetRawZeroPoseToStandingAbsoluteTrackingPose"); }
		uint32_t GetSortedTrackedDeviceIndicesOfClass(ETrackedDeviceClass, vr::TrackedDeviceIndex_t *, uint32_t, vr::TrackedDeviceIndex_t) override
		{ throw std::logic_error("unexpected IVRSystem::GetSortedTrackedDeviceIndicesOfClass"); }
		EDeviceActivityLevel GetTrackedDeviceActivityLevel(vr::TrackedDeviceIndex_t) override
		{ throw std::logic_error("unexpected IVRSystem::GetTrackedDeviceActivityLevel"); }
		void ApplyTransform(TrackedDevicePose_t *, const TrackedDevicePose_t *, const HmdMatrix34_t *) override
		{ throw std::logic_error("unexpected IVRSystem::ApplyTransform"); }
		vr::TrackedDeviceIndex_t GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole) override
		{ throw std::logic_error("unexpected IVRSystem::GetTrackedDeviceIndexForControllerRole"); }
		vr::ETrackedControllerRole GetControllerRoleForTrackedDeviceIndex(vr::TrackedDeviceIndex_t) override
		{ throw std::logic_error("unexpected IVRSystem::GetControllerRoleForTrackedDeviceIndex"); }
		ETrackedDeviceClass GetTrackedDeviceClass(vr::TrackedDeviceIndex_t) override
		{ throw std::logic_error("unexpected IVRSystem::GetTrackedDeviceClass"); }
		bool IsTrackedDeviceConnected(vr::TrackedDeviceIndex_t) override
		{ throw std::logic_error("unexpected IVRSystem::IsTrackedDeviceConnected"); }
		bool GetBoolTrackedDeviceProperty(vr::TrackedDeviceIndex_t, ETrackedDeviceProperty, ETrackedPropertyError *) override
		{ throw std::logic_error("unexpected IVRSystem::GetBoolTrackedDeviceProperty"); }
		float GetFloatTrackedDeviceProperty(vr::TrackedDeviceIndex_t, ETrackedDeviceProperty, ETrackedPropertyError *) override
		{ throw std::logic_error("unexpected IVRSystem::GetFloatTrackedDeviceProperty"); }
		int32_t GetInt32TrackedDeviceProperty(vr::TrackedDeviceIndex_t, ETrackedDeviceProperty, ETrackedPropertyError *) override
		{ throw std::logic_error("unexpected IVRSystem::GetInt32TrackedDeviceProperty"); }
		uint64_t GetUint64TrackedDeviceProperty(vr::TrackedDeviceIndex_t, ETrackedDeviceProperty, ETrackedPropertyError *) override
		{ throw std::logic_error("unexpected IVRSystem::GetUint64TrackedDeviceProperty"); }
		HmdMatrix34_t GetMatrix34TrackedDeviceProperty(vr::TrackedDeviceIndex_t, ETrackedDeviceProperty, ETrackedPropertyError *) override
		{ throw std::logic_error("unexpected IVRSystem::GetMatrix34TrackedDeviceProperty"); }
		uint32_t GetArrayTrackedDeviceProperty(vr::TrackedDeviceIndex_t, ETrackedDeviceProperty, PropertyTypeTag_t, void *, uint32_t, ETrackedPropertyError *) override
		{ throw std::logic_error("unexpected IVRSystem::GetArrayTrackedDeviceProperty"); }
		uint32_t GetStringTrackedDeviceProperty(vr::TrackedDeviceIndex_t, ETrackedDeviceProperty, char *, uint32_t, ETrackedPropertyError *) override
		{ throw std::logic_error("unexpected IVRSystem::GetStringTrackedDeviceProperty"); }
		const char *GetPropErrorNameFromEnum(ETrackedPropertyError) override
		{ throw std::logic_error("unexpected IVRSystem::GetPropErrorNameFromEnum"); }
		bool PollNextEvent(VREvent_t *, uint32_t) override
		{ throw std::logic_error("unexpected IVRSystem::PollNextEvent"); }
		bool PollNextEventWithPose(ETrackingUniverseOrigin, VREvent_t *, uint32_t, vr::TrackedDevicePose_t *) override
		{ throw std::logic_error("unexpected IVRSystem::PollNextEventWithPose"); }
		bool PollNextEventWithPoseAndOverlays(vr::ETrackingUniverseOrigin, VREvent_t *, uint32_t, TrackedDevicePose_t *, VROverlayHandle_t *) override
		{ throw std::logic_error("unexpected IVRSystem::PollNextEventWithPoseAndOverlays"); }
		const char *GetEventTypeNameFromEnum(EVREventType) override
		{ throw std::logic_error("unexpected IVRSystem::GetEventTypeNameFromEnum"); }
		HiddenAreaMesh_t GetHiddenAreaMesh(EVREye, EHiddenAreaMeshType) override
		{ throw std::logic_error("unexpected IVRSystem::GetHiddenAreaMesh"); }
		bool GetEyeTrackedFoveationCenter(HmdVector2_t *, HmdVector2_t *) override
		{ throw std::logic_error("unexpected IVRSystem::GetEyeTrackedFoveationCenter"); }
		bool GetEyeTrackedFoveationCenterForProjection(const HmdMatrix44_t *, HmdVector2_t *) override
		{ throw std::logic_error("unexpected IVRSystem::GetEyeTrackedFoveationCenterForProjection"); }
		bool GetControllerState(vr::TrackedDeviceIndex_t, vr::VRControllerState_t *, uint32_t) override
		{ throw std::logic_error("unexpected IVRSystem::GetControllerState"); }
		bool GetControllerStateWithPose(ETrackingUniverseOrigin, vr::TrackedDeviceIndex_t, vr::VRControllerState_t *, uint32_t, TrackedDevicePose_t *) override
		{ throw std::logic_error("unexpected IVRSystem::GetControllerStateWithPose"); }
		void TriggerHapticPulse(vr::TrackedDeviceIndex_t, uint32_t, unsigned short) override
		{ throw std::logic_error("unexpected IVRSystem::TriggerHapticPulse"); }
		const char *GetButtonIdNameFromEnum(EVRButtonId) override
		{ throw std::logic_error("unexpected IVRSystem::GetButtonIdNameFromEnum"); }
		const char *GetControllerAxisTypeNameFromEnum(EVRControllerAxisType) override
		{ throw std::logic_error("unexpected IVRSystem::GetControllerAxisTypeNameFromEnum"); }
		bool IsInputAvailable() override
		{ throw std::logic_error("unexpected IVRSystem::IsInputAvailable"); }
		bool IsSteamVRDrawingControllers() override
		{ throw std::logic_error("unexpected IVRSystem::IsSteamVRDrawingControllers"); }
		bool ShouldApplicationPause() override
		{ throw std::logic_error("unexpected IVRSystem::ShouldApplicationPause"); }
		bool ShouldApplicationReduceRenderingWork() override
		{ throw std::logic_error("unexpected IVRSystem::ShouldApplicationReduceRenderingWork"); }
		vr::EVRFirmwareError PerformFirmwareUpdate(vr::TrackedDeviceIndex_t) override
		{ throw std::logic_error("unexpected IVRSystem::PerformFirmwareUpdate"); }
		void AcknowledgeQuit_Exiting() override
		{ throw std::logic_error("unexpected IVRSystem::AcknowledgeQuit_Exiting"); }
		uint32_t GetAppContainerFilePaths(char *, uint32_t) override
		{ throw std::logic_error("unexpected IVRSystem::GetAppContainerFilePaths"); }
		const char *GetRuntimeVersion() override
		{ throw std::logic_error("unexpected IVRSystem::GetRuntimeVersion"); }
		vr::EVRInitError SetSDKVersion(uint32_t, uint32_t, uint32_t) override
		{ throw std::logic_error("unexpected IVRSystem::SetSDKVersion"); }
	};

}
