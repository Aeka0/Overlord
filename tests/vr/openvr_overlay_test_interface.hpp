#pragma once
#include <openvr.h>
#include <stdexcept>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4100)
#endif

// Strict SDK double generated from the vendored IVROverlay declarations.
namespace vr::tests
{
	struct overlay_api : IVROverlay
	{
		EVROverlayError FindOverlay( const char *pchOverlayKey, VROverlayHandle_t * pOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: FindOverlay");}
		EVROverlayError CreateOverlay( const char *pchOverlayKey, const char *pchOverlayName, VROverlayHandle_t * pOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: CreateOverlay");}
		EVROverlayError CreateSubviewOverlay( VROverlayHandle_t parentOverlayHandle, const char *pchSubviewOverlayKey, const char *pchSubviewOverlayName, VROverlayHandle_t *pSubviewOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: CreateSubviewOverlay");}
		EVROverlayError DestroyOverlay( VROverlayHandle_t ulOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: DestroyOverlay");}
		uint32_t GetOverlayKey( VROverlayHandle_t ulOverlayHandle, VR_OUT_STRING() char *pchValue, uint32_t unBufferSize, EVROverlayError *pError = 0L ) override {throw std::runtime_error("unexpected overlay call: GetOverlayKey");}
		uint32_t GetOverlayName( VROverlayHandle_t ulOverlayHandle, VR_OUT_STRING() char *pchValue, uint32_t unBufferSize, EVROverlayError *pError = 0L ) override {throw std::runtime_error("unexpected overlay call: GetOverlayName");}
		EVROverlayError SetOverlayName( VROverlayHandle_t ulOverlayHandle, const char *pchName ) override {throw std::runtime_error("unexpected overlay call: SetOverlayName");}
		EVROverlayError GetOverlayImageData( VROverlayHandle_t ulOverlayHandle, void *pvBuffer, uint32_t unBufferSize, uint32_t *punWidth, uint32_t *punHeight ) override {throw std::runtime_error("unexpected overlay call: GetOverlayImageData");}
		const char *GetOverlayErrorNameFromEnum( EVROverlayError error ) override {throw std::runtime_error("unexpected overlay call: GetOverlayErrorNameFromEnum");}
		EVROverlayError SetOverlayRenderingPid( VROverlayHandle_t ulOverlayHandle, uint32_t unPID ) override {throw std::runtime_error("unexpected overlay call: SetOverlayRenderingPid");}
		uint32_t GetOverlayRenderingPid( VROverlayHandle_t ulOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: GetOverlayRenderingPid");}
		EVROverlayError SetOverlayFlag( VROverlayHandle_t ulOverlayHandle, VROverlayFlags eOverlayFlag, bool bEnabled ) override {throw std::runtime_error("unexpected overlay call: SetOverlayFlag");}
		EVROverlayError GetOverlayFlag( VROverlayHandle_t ulOverlayHandle, VROverlayFlags eOverlayFlag, bool *pbEnabled ) override {throw std::runtime_error("unexpected overlay call: GetOverlayFlag");}
		EVROverlayError GetOverlayFlags( VROverlayHandle_t ulOverlayHandle, uint32_t *pFlags ) override {throw std::runtime_error("unexpected overlay call: GetOverlayFlags");}
		EVROverlayError SetOverlayColor( VROverlayHandle_t ulOverlayHandle, float fRed, float fGreen, float fBlue ) override {throw std::runtime_error("unexpected overlay call: SetOverlayColor");}
		EVROverlayError GetOverlayColor( VROverlayHandle_t ulOverlayHandle, float *pfRed, float *pfGreen, float *pfBlue ) override {throw std::runtime_error("unexpected overlay call: GetOverlayColor");}
		EVROverlayError SetOverlayAlpha( VROverlayHandle_t ulOverlayHandle, float fAlpha ) override {throw std::runtime_error("unexpected overlay call: SetOverlayAlpha");}
		EVROverlayError GetOverlayAlpha( VROverlayHandle_t ulOverlayHandle, float *pfAlpha ) override {throw std::runtime_error("unexpected overlay call: GetOverlayAlpha");}
		EVROverlayError SetOverlayTexelAspect( VROverlayHandle_t ulOverlayHandle, float fTexelAspect ) override {throw std::runtime_error("unexpected overlay call: SetOverlayTexelAspect");}
		EVROverlayError GetOverlayTexelAspect( VROverlayHandle_t ulOverlayHandle, float *pfTexelAspect ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTexelAspect");}
		EVROverlayError SetOverlaySortOrder( VROverlayHandle_t ulOverlayHandle, uint32_t unSortOrder ) override {throw std::runtime_error("unexpected overlay call: SetOverlaySortOrder");}
		EVROverlayError GetOverlaySortOrder( VROverlayHandle_t ulOverlayHandle, uint32_t *punSortOrder ) override {throw std::runtime_error("unexpected overlay call: GetOverlaySortOrder");}
		EVROverlayError SetOverlayWidthInMeters( VROverlayHandle_t ulOverlayHandle, float fWidthInMeters ) override {throw std::runtime_error("unexpected overlay call: SetOverlayWidthInMeters");}
		EVROverlayError GetOverlayWidthInMeters( VROverlayHandle_t ulOverlayHandle, float *pfWidthInMeters ) override {throw std::runtime_error("unexpected overlay call: GetOverlayWidthInMeters");}
		EVROverlayError SetOverlayCurvature( VROverlayHandle_t ulOverlayHandle, float fCurvature ) override {throw std::runtime_error("unexpected overlay call: SetOverlayCurvature");}
		EVROverlayError GetOverlayCurvature( VROverlayHandle_t ulOverlayHandle, float *pfCurvature ) override {throw std::runtime_error("unexpected overlay call: GetOverlayCurvature");}
		EVROverlayError SetOverlayPreCurvePitch( VROverlayHandle_t ulOverlayHandle, float fRadians ) override {throw std::runtime_error("unexpected overlay call: SetOverlayPreCurvePitch");}
		EVROverlayError GetOverlayPreCurvePitch( VROverlayHandle_t ulOverlayHandle, float *pfRadians ) override {throw std::runtime_error("unexpected overlay call: GetOverlayPreCurvePitch");}
		EVROverlayError SetOverlayTextureColorSpace( VROverlayHandle_t ulOverlayHandle, EColorSpace eTextureColorSpace ) override {throw std::runtime_error("unexpected overlay call: SetOverlayTextureColorSpace");}
		EVROverlayError GetOverlayTextureColorSpace( VROverlayHandle_t ulOverlayHandle, EColorSpace *peTextureColorSpace ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTextureColorSpace");}
		EVROverlayError SetOverlayTextureBounds( VROverlayHandle_t ulOverlayHandle, const VRTextureBounds_t *pOverlayTextureBounds ) override {throw std::runtime_error("unexpected overlay call: SetOverlayTextureBounds");}
		EVROverlayError GetOverlayTextureBounds( VROverlayHandle_t ulOverlayHandle, VRTextureBounds_t *pOverlayTextureBounds ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTextureBounds");}
		EVROverlayError GetOverlayTransformType( VROverlayHandle_t ulOverlayHandle, VROverlayTransformType *peTransformType ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTransformType");}
		EVROverlayError SetOverlayTransformAbsolute( VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin eTrackingOrigin, const HmdMatrix34_t *pmatTrackingOriginToOverlayTransform ) override {throw std::runtime_error("unexpected overlay call: SetOverlayTransformAbsolute");}
		EVROverlayError GetOverlayTransformAbsolute( VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin *peTrackingOrigin, HmdMatrix34_t *pmatTrackingOriginToOverlayTransform ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTransformAbsolute");}
		EVROverlayError SetOverlayTransformTrackedDeviceRelative( VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t unTrackedDevice, const HmdMatrix34_t *pmatTrackedDeviceToOverlayTransform ) override {throw std::runtime_error("unexpected overlay call: SetOverlayTransformTrackedDeviceRelative");}
		EVROverlayError GetOverlayTransformTrackedDeviceRelative( VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t *punTrackedDevice, HmdMatrix34_t *pmatTrackedDeviceToOverlayTransform ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTransformTrackedDeviceRelative");}
		EVROverlayError SetOverlayTransformTrackedDeviceComponent( VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t unDeviceIndex, const char *pchComponentName ) override {throw std::runtime_error("unexpected overlay call: SetOverlayTransformTrackedDeviceComponent");}
		EVROverlayError GetOverlayTransformTrackedDeviceComponent( VROverlayHandle_t ulOverlayHandle, TrackedDeviceIndex_t *punDeviceIndex, VR_OUT_STRING() char *pchComponentName, uint32_t unComponentNameSize ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTransformTrackedDeviceComponent");}
		EVROverlayError SetOverlayTransformCursor( VROverlayHandle_t ulCursorOverlayHandle, const HmdVector2_t *pvHotspot ) override {throw std::runtime_error("unexpected overlay call: SetOverlayTransformCursor");}
		vr::EVROverlayError GetOverlayTransformCursor( VROverlayHandle_t ulOverlayHandle, HmdVector2_t *pvHotspot ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTransformCursor");}
		vr::EVROverlayError SetOverlayTransformProjection( VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin eTrackingOrigin, const HmdMatrix34_t* pmatTrackingOriginToOverlayTransform, const VROverlayProjection_t *pProjection, vr::EVREye eEye ) override {throw std::runtime_error("unexpected overlay call: SetOverlayTransformProjection");}
		EVROverlayError SetSubviewPosition( VROverlayHandle_t ulOverlayHandle, float fX, float fY ) override {throw std::runtime_error("unexpected overlay call: SetSubviewPosition");}
		EVROverlayError ShowOverlay( VROverlayHandle_t ulOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: ShowOverlay");}
		EVROverlayError HideOverlay( VROverlayHandle_t ulOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: HideOverlay");}
		bool IsOverlayVisible( VROverlayHandle_t ulOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: IsOverlayVisible");}
		EVROverlayError GetTransformForOverlayCoordinates( VROverlayHandle_t ulOverlayHandle, ETrackingUniverseOrigin eTrackingOrigin, HmdVector2_t coordinatesInOverlay, HmdMatrix34_t *pmatTransform ) override {throw std::runtime_error("unexpected overlay call: GetTransformForOverlayCoordinates");}
		EVROverlayError WaitFrameSync( uint32_t nTimeoutMs ) override {throw std::runtime_error("unexpected overlay call: WaitFrameSync");}
		bool PollNextOverlayEvent( VROverlayHandle_t ulOverlayHandle, VREvent_t *pEvent, uint32_t uncbVREvent ) override {throw std::runtime_error("unexpected overlay call: PollNextOverlayEvent");}
		EVROverlayError GetOverlayInputMethod( VROverlayHandle_t ulOverlayHandle, VROverlayInputMethod *peInputMethod ) override {throw std::runtime_error("unexpected overlay call: GetOverlayInputMethod");}
		EVROverlayError SetOverlayInputMethod( VROverlayHandle_t ulOverlayHandle, VROverlayInputMethod eInputMethod ) override {throw std::runtime_error("unexpected overlay call: SetOverlayInputMethod");}
		EVROverlayError GetOverlayMouseScale( VROverlayHandle_t ulOverlayHandle, HmdVector2_t *pvecMouseScale ) override {throw std::runtime_error("unexpected overlay call: GetOverlayMouseScale");}
		EVROverlayError SetOverlayMouseScale( VROverlayHandle_t ulOverlayHandle, const HmdVector2_t *pvecMouseScale ) override {throw std::runtime_error("unexpected overlay call: SetOverlayMouseScale");}
		bool ComputeOverlayIntersection( VROverlayHandle_t ulOverlayHandle, const VROverlayIntersectionParams_t *pParams, VROverlayIntersectionResults_t *pResults ) override {throw std::runtime_error("unexpected overlay call: ComputeOverlayIntersection");}
		bool IsHoverTargetOverlay( VROverlayHandle_t ulOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: IsHoverTargetOverlay");}
		EVROverlayError SetOverlayIntersectionMask( VROverlayHandle_t ulOverlayHandle, VROverlayIntersectionMaskPrimitive_t *pMaskPrimitives, uint32_t unNumMaskPrimitives, uint32_t unPrimitiveSize = sizeof( VROverlayIntersectionMaskPrimitive_t ) ) override {throw std::runtime_error("unexpected overlay call: SetOverlayIntersectionMask");}
		EVROverlayError TriggerLaserMouseHapticVibration( VROverlayHandle_t ulOverlayHandle, float fDurationSeconds, float fFrequency, float fAmplitude ) override {throw std::runtime_error("unexpected overlay call: TriggerLaserMouseHapticVibration");}
		EVROverlayError SetOverlayCursor( VROverlayHandle_t ulOverlayHandle, VROverlayHandle_t ulCursorHandle ) override {throw std::runtime_error("unexpected overlay call: SetOverlayCursor");}
		EVROverlayError SetOverlayCursorPositionOverride( VROverlayHandle_t ulOverlayHandle, const HmdVector2_t *pvCursor ) override {throw std::runtime_error("unexpected overlay call: SetOverlayCursorPositionOverride");}
		EVROverlayError ClearOverlayCursorPositionOverride( VROverlayHandle_t ulOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: ClearOverlayCursorPositionOverride");}
		EVROverlayError SetOverlayTexture( VROverlayHandle_t ulOverlayHandle, const Texture_t *pTexture ) override {throw std::runtime_error("unexpected overlay call: SetOverlayTexture");}
		EVROverlayError ClearOverlayTexture( VROverlayHandle_t ulOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: ClearOverlayTexture");}
		EVROverlayError SetOverlayRaw( VROverlayHandle_t ulOverlayHandle, void *pvBuffer, uint32_t unWidth, uint32_t unHeight, uint32_t unBytesPerPixel ) override {throw std::runtime_error("unexpected overlay call: SetOverlayRaw");}
		EVROverlayError SetOverlayFromFile( VROverlayHandle_t ulOverlayHandle, const char *pchFilePath ) override {throw std::runtime_error("unexpected overlay call: SetOverlayFromFile");}
		EVROverlayError GetOverlayTexture( VROverlayHandle_t ulOverlayHandle, void **pNativeTextureHandle, void *pNativeTextureRef, uint32_t *pWidth, uint32_t *pHeight, uint32_t *pNativeFormat, ETextureType *pAPIType, EColorSpace *pColorSpace, VRTextureBounds_t *pTextureBounds ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTexture");}
		EVROverlayError ReleaseNativeOverlayHandle( VROverlayHandle_t ulOverlayHandle, void *pNativeTextureHandle ) override {throw std::runtime_error("unexpected overlay call: ReleaseNativeOverlayHandle");}
		EVROverlayError GetOverlayTextureSize( VROverlayHandle_t ulOverlayHandle, uint32_t *pWidth, uint32_t *pHeight ) override {throw std::runtime_error("unexpected overlay call: GetOverlayTextureSize");}
		EVROverlayError CreateDashboardOverlay( const char *pchOverlayKey, const char *pchOverlayFriendlyName, VROverlayHandle_t * pMainHandle, VROverlayHandle_t *pThumbnailHandle ) override {throw std::runtime_error("unexpected overlay call: CreateDashboardOverlay");}
		bool IsDashboardVisible() override {throw std::runtime_error("unexpected overlay call: IsDashboardVisible");}
		bool IsActiveDashboardOverlay( VROverlayHandle_t ulOverlayHandle ) override {throw std::runtime_error("unexpected overlay call: IsActiveDashboardOverlay");}
		EVROverlayError SetDashboardOverlaySceneProcess( VROverlayHandle_t ulOverlayHandle, uint32_t unProcessId ) override {throw std::runtime_error("unexpected overlay call: SetDashboardOverlaySceneProcess");}
		EVROverlayError GetDashboardOverlaySceneProcess( VROverlayHandle_t ulOverlayHandle, uint32_t *punProcessId ) override {throw std::runtime_error("unexpected overlay call: GetDashboardOverlaySceneProcess");}
		void ShowDashboard( const char *pchOverlayToShow ) override {throw std::runtime_error("unexpected overlay call: ShowDashboard");}
		vr::TrackedDeviceIndex_t GetPrimaryDashboardDevice() override {throw std::runtime_error("unexpected overlay call: GetPrimaryDashboardDevice");}
		EVROverlayError ShowKeyboard( EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode, uint32_t unFlags, const char *pchDescription, uint32_t unCharMax, const char *pchExistingText, uint64_t uUserValue ) override {throw std::runtime_error("unexpected overlay call: ShowKeyboard");}
		EVROverlayError ShowKeyboardForOverlay( VROverlayHandle_t ulOverlayHandle, EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode, uint32_t unFlags, const char *pchDescription, uint32_t unCharMax, const char *pchExistingText, uint64_t uUserValue ) override {throw std::runtime_error("unexpected overlay call: ShowKeyboardForOverlay");}
		uint32_t GetKeyboardText( VR_OUT_STRING() char *pchText, uint32_t cchText ) override {throw std::runtime_error("unexpected overlay call: GetKeyboardText");}
		void HideKeyboard() override {throw std::runtime_error("unexpected overlay call: HideKeyboard");}
		void SetKeyboardTransformAbsolute( ETrackingUniverseOrigin eTrackingOrigin, const HmdMatrix34_t *pmatTrackingOriginToKeyboardTransform ) override {throw std::runtime_error("unexpected overlay call: SetKeyboardTransformAbsolute");}
		void SetKeyboardPositionForOverlay( VROverlayHandle_t ulOverlayHandle, HmdRect2_t avoidRect ) override {throw std::runtime_error("unexpected overlay call: SetKeyboardPositionForOverlay");}
		VRMessageOverlayResponse ShowMessageOverlay( const char* pchText, const char* pchCaption, const char* pchButton0Text, const char* pchButton1Text = nullptr, const char* pchButton2Text = nullptr, const char* pchButton3Text = nullptr ) override {throw std::runtime_error("unexpected overlay call: ShowMessageOverlay");}
		void CloseMessageOverlay() override {throw std::runtime_error("unexpected overlay call: CloseMessageOverlay");}
	};
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
