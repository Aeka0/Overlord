#include <std_include.hpp>

#include "openxr_dispatch.hpp"

#include <format>

#if H2V_OPENXR_HEADERS_AVAILABLE
namespace vr::openxr
{
	namespace
	{
		template <typename Function>
		bool load_function(PFN_xrGetInstanceProcAddr get_instance_proc_addr, const XrInstance instance,
			const char* name, Function& output, std::string& error, XrResult& result)
		{
			PFN_xrVoidFunction function{};
			result = get_instance_proc_addr(instance, name, &function);
			if (XR_FAILED(result) || function == nullptr)
			{
				if (function == nullptr && XR_SUCCEEDED(result))
				{
					result = XR_ERROR_FUNCTION_UNSUPPORTED;
				}
				error = std::format("{} is unavailable (XrResult={})", name, static_cast<std::int64_t>(result));
				return false;
			}

			output = reinterpret_cast<Function>(function);
			return true;
		}
	}

	bool dispatch_table::load_global(const PFN_xrGetInstanceProcAddr entry, std::string& error, XrResult& result)
	{
		reset();
		if (entry == nullptr)
		{
			result = XR_ERROR_FUNCTION_UNSUPPORTED;
			error = "xrGetInstanceProcAddr export is unavailable";
			return false;
		}

		get_instance_proc_addr = entry;
		return load_function(entry, XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
			enumerate_instance_extension_properties, error, result) &&
			load_function(entry, XR_NULL_HANDLE, "xrCreateInstance", create_instance, error, result);
	}

	bool dispatch_table::load_instance(const XrInstance instance, std::string& error, XrResult& result)
	{
#define H2V_LOAD_XR(member, name) \
		if (!load_function(get_instance_proc_addr, instance, name, member, error, result)) return false

		H2V_LOAD_XR(destroy_instance, "xrDestroyInstance");
		H2V_LOAD_XR(get_instance_properties, "xrGetInstanceProperties");
		H2V_LOAD_XR(get_system, "xrGetSystem");
		H2V_LOAD_XR(get_system_properties, "xrGetSystemProperties");
		H2V_LOAD_XR(enumerate_view_configurations, "xrEnumerateViewConfigurations");
		H2V_LOAD_XR(enumerate_view_configuration_views, "xrEnumerateViewConfigurationViews");
		H2V_LOAD_XR(enumerate_environment_blend_modes, "xrEnumerateEnvironmentBlendModes");
		H2V_LOAD_XR(get_d3d11_graphics_requirements, "xrGetD3D11GraphicsRequirementsKHR");
		H2V_LOAD_XR(create_session, "xrCreateSession");
		H2V_LOAD_XR(destroy_session, "xrDestroySession");
		H2V_LOAD_XR(create_reference_space, "xrCreateReferenceSpace");
		H2V_LOAD_XR(destroy_space, "xrDestroySpace");
		H2V_LOAD_XR(enumerate_swapchain_formats, "xrEnumerateSwapchainFormats");
		H2V_LOAD_XR(create_swapchain, "xrCreateSwapchain");
		H2V_LOAD_XR(destroy_swapchain, "xrDestroySwapchain");
		H2V_LOAD_XR(enumerate_swapchain_images, "xrEnumerateSwapchainImages");
		H2V_LOAD_XR(acquire_swapchain_image, "xrAcquireSwapchainImage");
		H2V_LOAD_XR(wait_swapchain_image, "xrWaitSwapchainImage");
		H2V_LOAD_XR(release_swapchain_image, "xrReleaseSwapchainImage");
		H2V_LOAD_XR(poll_event, "xrPollEvent");
		H2V_LOAD_XR(begin_session, "xrBeginSession");
		H2V_LOAD_XR(end_session, "xrEndSession");
		H2V_LOAD_XR(wait_frame, "xrWaitFrame");
		H2V_LOAD_XR(begin_frame, "xrBeginFrame");
		H2V_LOAD_XR(locate_views, "xrLocateViews");
		H2V_LOAD_XR(end_frame, "xrEndFrame");

#undef H2V_LOAD_XR
		return true;
	}

	void dispatch_table::reset() noexcept
	{
		*this = {};
	}
}
#endif
