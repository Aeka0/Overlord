#pragma once

#include <string>

#if __has_include(<openxr/openxr.h>)
#define H2V_OPENXR_HEADERS_AVAILABLE 1

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vr::openxr
{
	struct call_result
	{
		XrResult code{XR_SUCCESS};
		const char* operation{};
		explicit operator bool() const noexcept
		{
			// A timeout leaves image ownership pending even though its code is positive.
			return XR_SUCCEEDED(code) && code != XR_TIMEOUT_EXPIRED;
		}
	};

	struct dispatch_table
	{
		PFN_xrGetInstanceProcAddr get_instance_proc_addr{};
		PFN_xrEnumerateInstanceExtensionProperties enumerate_instance_extension_properties{};
		using enumerate_instance_version_fn = XrResult(XRAPI_PTR*)(XrVersion* apiVersion);
		enumerate_instance_version_fn enumerate_instance_version{};
		PFN_xrCreateInstance create_instance{};
		PFN_xrDestroyInstance destroy_instance{};
		PFN_xrGetInstanceProperties get_instance_properties{};
		PFN_xrGetSystem get_system{};
		PFN_xrGetSystemProperties get_system_properties{};
		PFN_xrEnumerateViewConfigurations enumerate_view_configurations{};
		PFN_xrEnumerateViewConfigurationViews enumerate_view_configuration_views{};
		PFN_xrEnumerateEnvironmentBlendModes enumerate_environment_blend_modes{};
		PFN_xrGetD3D11GraphicsRequirementsKHR get_d3d11_graphics_requirements{};
		PFN_xrCreateSession create_session{};
		PFN_xrDestroySession destroy_session{};
		PFN_xrCreateReferenceSpace create_reference_space{};
		PFN_xrDestroySpace destroy_space{};
		PFN_xrEnumerateSwapchainFormats enumerate_swapchain_formats{};
		PFN_xrCreateSwapchain create_swapchain{};
		PFN_xrDestroySwapchain destroy_swapchain{};
		PFN_xrEnumerateSwapchainImages enumerate_swapchain_images{};
		PFN_xrAcquireSwapchainImage acquire_swapchain_image{};
		PFN_xrWaitSwapchainImage wait_swapchain_image{};
		PFN_xrReleaseSwapchainImage release_swapchain_image{};
		PFN_xrPollEvent poll_event{};
		PFN_xrBeginSession begin_session{};
		PFN_xrEndSession end_session{};
		PFN_xrWaitFrame wait_frame{};
		PFN_xrBeginFrame begin_frame{};
		PFN_xrLocateViews locate_views{};
		PFN_xrEndFrame end_frame{};
		PFN_xrStringToPath string_to_path{};
		PFN_xrCreateActionSet create_action_set{};
		PFN_xrDestroyActionSet destroy_action_set{};
		PFN_xrCreateAction create_action{};
		PFN_xrSuggestInteractionProfileBindings suggest_interaction_profile_bindings{};
		PFN_xrAttachSessionActionSets attach_session_action_sets{};
		PFN_xrCreateActionSpace create_action_space{};
		PFN_xrLocateSpace locate_space{};
		PFN_xrSyncActions sync_actions{};
		PFN_xrGetCurrentInteractionProfile get_current_interaction_profile{};
		PFN_xrGetActionStateBoolean get_action_state_boolean{};
		PFN_xrGetActionStateFloat get_action_state_float{};
		PFN_xrGetActionStateVector2f get_action_state_vector2f{};
		PFN_xrGetActionStatePose get_action_state_pose{};
		PFN_xrApplyHapticFeedback apply_haptic_feedback{};

		[[nodiscard]] bool load_global(PFN_xrGetInstanceProcAddr entry, std::string& error, XrResult& result);
		[[nodiscard]] bool load_instance(XrInstance instance, std::string& error, XrResult& result);
		void reset() noexcept;
	};
}

#else
#define H2V_OPENXR_HEADERS_AVAILABLE 0

namespace vr::openxr
{
	struct dispatch_table
	{
	};
}
#endif
