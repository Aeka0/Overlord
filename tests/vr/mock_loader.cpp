#include <std_include.hpp>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "mock_control.hpp"
#include <mutex>

struct XrInstance_T
{
	std::uint64_t id{};
};
struct XrAction_T { XrActionSet owner{}; std::string name; XrActionType type{}; bool bound{}; };
struct XrActionSet_T { XrInstance owner{}; std::vector<XrAction> actions; };

struct XrSession_T
{
	std::uint64_t id{};
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	std::vector<XrSwapchain> swapchains;
	std::vector<XrSpace> spaces;
	XrSessionState state{XR_SESSION_STATE_IDLE};
	bool running{};
	bool frame_waited{};
	bool frame_begun{};
	std::array<XrView, 2> located_views{};
	XrActionSet actions{};bool synced{};XrTime display_time{};
};

struct XrSpace_T
{
	std::uint64_t id{};
	XrSession owner{XR_NULL_HANDLE};
	XrReferenceSpaceType reference_type{XR_REFERENCE_SPACE_TYPE_LOCAL};XrAction action{};
};

struct XrSwapchain_T
{
	std::uint64_t id{};
	XrSession owner{XR_NULL_HANDLE};
	std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> textures;
	std::uint32_t next_image{};
	bool acquired{};
	bool waited{};
};

namespace
{
	using vr::tests::mock::failure_point;
	using vr::tests::mock::scenario;
	using vr::tests::mock::statistics;

	std::mutex g_mutex;
	scenario g_scenario{scenario::happy};
	std::deque<std::pair<failure_point, XrResult>> g_failures;
	LUID g_adapter_luid{};
	D3D_FEATURE_LEVEL g_minimum_feature_level{D3D_FEATURE_LEVEL_10_0};
	XrBool32 g_should_render{XR_TRUE};
	constexpr auto tracked_pose_flags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT |
	                                    XR_VIEW_STATE_POSITION_TRACKED_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
	XrViewStateFlags g_view_flags = tracked_pose_flags;
	std::array<XrSpaceLocationFlags, 2> g_hand_flags{tracked_pose_flags, tracked_pose_flags};
	float g_head_height{};
	std::string g_runtime_name{"h2v mock OpenXR runtime"};
	std::array<std::string, 2> g_profiles{ "/interaction_profiles/oculus/touch_controller", "/interaction_profiles/oculus/touch_controller" };
	bool g_profile_changed{};
	bool g_wait_timeout_returned{};
	std::deque<XrSessionState> g_session_states;
	statistics g_statistics;
	bool g_synthetic_checks=true;
	bool g_cylinder_supported=false;
	unsigned g_eye_width=64,g_eye_height=64;
	std::unordered_map<XrPath,std::string> g_paths;
	std::unordered_map<std::string,std::array<float,2>> g_action_values;
	XrSession g_current_session{XR_NULL_HANDLE};
	std::uint64_t g_next_handle{1};
	XrTime g_next_display_time{1};
	std::array<std::uint8_t, 2> g_last_texture_epochs{};
	bool g_has_texture_epoch{};

	bool consume_failure_locked(const failure_point point, XrResult& result)
	{
		const auto failure = std::find_if(g_failures.begin(), g_failures.end(), [point](const auto& entry)
		{
			return entry.first == point;
		});
		if (failure == g_failures.end())
		{
			return false;
		}
		result = failure->second;
		g_failures.erase(failure);
		return true;
	}

	template <typename T>
	XrResult enumerate_one(const T value, const std::uint32_t capacity, std::uint32_t* const count, T* const output)
	{
		if (count == nullptr)
		{
			return XR_ERROR_VALIDATION_FAILURE;
		}
		*count = 1;
		if (capacity == 0)
		{
			return XR_SUCCESS;
		}
		if (capacity < 1 || output == nullptr)
		{
			return XR_ERROR_SIZE_INSUFFICIENT;
		}
		output[0] = value;
		return XR_SUCCESS;
	}

	template <typename Function>
	void assign_proc(PFN_xrVoidFunction* const output, const Function function)
	{
		*output = reinterpret_cast<PFN_xrVoidFunction>(function);
	}

	bool read_first_pixel(const XrSession session, const XrSwapchain swapchain,
		std::array<std::uint8_t, 4>& pixel)
	{
		if (session == XR_NULL_HANDLE || swapchain == XR_NULL_HANDLE || swapchain->owner != session ||
			swapchain->textures.empty() || swapchain->next_image == 0)
		{
			return false;
		}
		const auto image_index = (swapchain->next_image - 1) % swapchain->textures.size();
		D3D11_TEXTURE2D_DESC source_description{};
		swapchain->textures[image_index]->GetDesc(&source_description);
		D3D11_TEXTURE2D_DESC staging_description = source_description;
		staging_description.Width = 1;
		staging_description.Height = 1;
		staging_description.MipLevels = 1;
		staging_description.ArraySize = 1;
		staging_description.SampleDesc = {1, 0};
		staging_description.Usage = D3D11_USAGE_STAGING;
		staging_description.BindFlags = 0;
		staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		staging_description.MiscFlags = 0;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
		if (FAILED(session->device->CreateTexture2D(&staging_description, nullptr, &staging)))
		{
			return false;
		}
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		session->device->GetImmediateContext(&context);
		const D3D11_BOX source_box{0, 0, 0, 1, 1, 1};
		context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0,
			swapchain->textures[image_index].Get(), 0, &source_box);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
		{
			return false;
		}
		std::memcpy(pixel.data(), mapped.pData, pixel.size());
		context->Unmap(staging.Get(), 0);
		return true;
	}
}

XrResult XRAPI_CALL mockDestroySwapchain(XrSwapchain swapchain);

extern "C" __declspec(dllexport) void WINAPI h2vMockReset()
{
	const std::lock_guard lock(g_mutex);
	g_scenario = scenario::happy;
	g_failures.clear();
	g_adapter_luid = {};
	g_minimum_feature_level = D3D_FEATURE_LEVEL_10_0;
	g_should_render = XR_TRUE;
	g_view_flags = tracked_pose_flags;
	g_hand_flags = {tracked_pose_flags, tracked_pose_flags};
	g_head_height = 0;
	g_runtime_name = "h2v mock OpenXR runtime";
	g_profiles.fill("/interaction_profiles/oculus/touch_controller");
	g_profile_changed = false;
	g_wait_timeout_returned = false;
	g_session_states.clear();
	g_statistics = {};
	g_paths.clear();g_action_values.clear();g_synthetic_checks=true;g_cylinder_supported=false;g_eye_width=g_eye_height=64;
	g_next_display_time = 1;
	g_last_texture_epochs = {};
	g_has_texture_epoch = false;
}

extern "C" __declspec(dllexport) void WINAPI h2vMockSetScenario(const scenario value)
{
	const std::lock_guard lock(g_mutex);
	g_scenario = value;
}

extern "C" __declspec(dllexport) void WINAPI h2vMockFailOnce(
	const failure_point point, const std::int32_t result)
{
	const std::lock_guard lock(g_mutex);
	g_failures.emplace_back(point, static_cast<XrResult>(result));
}

extern "C" __declspec(dllexport) void WINAPI h2vMockSetGraphicsRequirements(
	const LUID adapter_luid, const D3D_FEATURE_LEVEL minimum_feature_level)
{
	const std::lock_guard lock(g_mutex);
	g_adapter_luid = adapter_luid;
	g_minimum_feature_level = minimum_feature_level;
}

extern "C" __declspec(dllexport) void WINAPI h2vMockSetShouldRender(const BOOL should_render)
{
	const std::lock_guard lock(g_mutex);
	g_should_render = should_render ? XR_TRUE : XR_FALSE;
}

extern "C" __declspec(dllexport) void WINAPI h2vMockSetEyeExtent(std::uint32_t width,std::uint32_t height)
{const std::lock_guard lock(g_mutex);g_eye_width=width;g_eye_height=height;}

extern "C" __declspec(dllexport) void WINAPI h2vMockSetCylinderSupported(BOOL enabled){const std::lock_guard lock(g_mutex);g_cylinder_supported=enabled!=FALSE;}
extern "C" __declspec(dllexport) void WINAPI h2vMockSetSyntheticChecks(BOOL enabled)
{ const std::lock_guard lock(g_mutex);g_synthetic_checks=enabled!=FALSE; }

extern "C" __declspec(dllexport) void WINAPI h2vMockSetActionValue(const char* action,float x,float y)
{ const std::lock_guard lock(g_mutex);if(action)g_action_values[action]={x,y}; }

extern "C" __declspec(dllexport) void WINAPI h2vMockSetHandFlags(unsigned hand, const std::uint64_t flags)
{
	const std::lock_guard lock(g_mutex);
	if (hand < g_hand_flags.size()) g_hand_flags[hand] = flags;
}
extern "C" __declspec(dllexport) void WINAPI h2vMockSetViewFlags(const std::uint64_t flags)
{
	const std::lock_guard lock(g_mutex);
	g_view_flags = flags;
}

extern "C" __declspec(dllexport) void WINAPI h2vMockSetHeadHeight(float height)
{ const std::lock_guard lock(g_mutex); g_head_height = height; }

extern "C" __declspec(dllexport) void WINAPI h2vMockSetRuntimeName(const char* name)
{ const std::lock_guard lock(g_mutex); if (name) g_runtime_name = name; }

extern "C" __declspec(dllexport) void WINAPI h2vMockSetInteractionProfile(unsigned hand, const char* profile)
{
	const std::lock_guard lock(g_mutex);
	if (hand < g_profiles.size() && profile) { g_profiles[hand] = profile; g_profile_changed = true; }
}

extern "C" __declspec(dllexport) void WINAPI h2vMockQueueSessionState(const std::int32_t state)
{
	const std::lock_guard lock(g_mutex);
	g_session_states.push_back(static_cast<XrSessionState>(state));
}

extern "C" __declspec(dllexport) void WINAPI h2vMockGetStatistics(statistics* const output)
{
	if (output == nullptr)
	{
		return;
	}
	const std::lock_guard lock(g_mutex);
	*output = g_statistics;
}

extern "C" __declspec(dllexport) std::int32_t WINAPI h2vMockDestroyAcquiredSwapchain()
{
	XrSwapchain acquired_swapchain{XR_NULL_HANDLE};
	{
		const std::lock_guard lock(g_mutex);
		if (g_current_session != XR_NULL_HANDLE)
		{
			const auto found = std::find_if(g_current_session->swapchains.begin(),
				g_current_session->swapchains.end(), [](const auto swapchain)
				{
					return swapchain->acquired;
				});
			if (found != g_current_session->swapchains.end())
			{
				acquired_swapchain = *found;
			}
		}
	}
	return mockDestroySwapchain(acquired_swapchain);
}

XrResult XRAPI_CALL mockEnumerateInstanceExtensionProperties(const char*, const std::uint32_t capacity,
	std::uint32_t* const count, XrExtensionProperties* const properties)
{
	const std::lock_guard lock(g_mutex);
	if (g_scenario == scenario::runtime_unavailable)
	{
		return XR_ERROR_RUNTIME_UNAVAILABLE;
	}
	if (count == nullptr)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	*count = g_cylinder_supported ? 2 : 1;
	if (capacity == 0)
	{
		return XR_SUCCESS;
	}
	if (capacity < *count || properties == nullptr)
	{
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	properties[0].type = XR_TYPE_EXTENSION_PROPERTIES;
	properties[0].next = nullptr;
	strcpy_s(properties[0].extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
	properties[0].extensionVersion = XR_KHR_D3D11_enable_SPEC_VERSION;
	if(g_cylinder_supported){properties[1]={XR_TYPE_EXTENSION_PROPERTIES};strcpy_s(properties[1].extensionName,XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);properties[1].extensionVersion=XR_KHR_composition_layer_cylinder_SPEC_VERSION;}
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEnumerateInstanceVersion(XrVersion* const version)
{
	if (version == nullptr)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	*version = XR_CURRENT_API_VERSION;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockCreateInstance(const XrInstanceCreateInfo* const create_info, XrInstance* const instance)
{
	if (create_info == nullptr || instance == nullptr || create_info->enabledExtensionCount != (g_cylinder_supported?2u:1u) ||
		create_info->enabledExtensionNames == nullptr ||
		std::strcmp(create_info->enabledExtensionNames[0], XR_KHR_D3D11_ENABLE_EXTENSION_NAME) != 0 ||
		(g_cylinder_supported && std::strcmp(create_info->enabledExtensionNames[1],XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME)!=0))
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	const std::lock_guard lock(g_mutex);
	*instance = new XrInstance_T{g_next_handle++};
	++g_statistics.instances_created;
	if (GetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr, 0) != 0)
		++g_statistics.instances_with_runtime_override;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockDestroyInstance(const XrInstance instance)
{
	if (instance == XR_NULL_HANDLE)
	{
		return XR_ERROR_HANDLE_INVALID;
	}
	{
		const std::lock_guard lock(g_mutex);
		XrResult injected_result{};
		if (consume_failure_locked(failure_point::destroy_instance, injected_result))
		{
			return injected_result;
		}
		if (g_current_session != XR_NULL_HANDLE)
		{
			return XR_ERROR_CALL_ORDER_INVALID;
		}
		++g_statistics.instances_destroyed;
	}
	delete instance;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockGetInstanceProperties(XrInstance, XrInstanceProperties* const properties)
{
	if (properties == nullptr)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	properties->runtimeVersion = XR_MAKE_VERSION(1, 1, 0);
	const std::lock_guard lock(g_mutex);
	strcpy_s(properties->runtimeName, g_runtime_name.c_str());
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockGetSystem(XrInstance, const XrSystemGetInfo* const get_info, XrSystemId* const system_id)
{
	if (get_info == nullptr || system_id == nullptr || get_info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	const std::lock_guard lock(g_mutex);
	if (g_scenario == scenario::no_hmd)
	{
		return XR_ERROR_FORM_FACTOR_UNAVAILABLE;
	}
	*system_id = 1;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockGetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* const properties)
{
	if (properties == nullptr)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	properties->systemId = 1;
	properties->vendorId = 0x48325652;
	strcpy_s(properties->systemName, "h2v mock HMD");
	properties->graphicsProperties = {4096, 4096, 16};
	properties->trackingProperties = {XR_TRUE, XR_TRUE};
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEnumerateViewConfigurations(XrInstance, XrSystemId, const std::uint32_t capacity,
	std::uint32_t* const count, XrViewConfigurationType* const configurations)
{
	return enumerate_one(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, capacity, count, configurations);
}

XrResult XRAPI_CALL mockEnumerateViewConfigurationViews(XrInstance, XrSystemId,
	const XrViewConfigurationType configuration, const std::uint32_t capacity, std::uint32_t* const count,
	XrViewConfigurationView* const views)
{
	if (configuration != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO || count == nullptr)
	{
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	}
	*count = 2;
	if (capacity == 0)
	{
		return XR_SUCCESS;
	}
	if (capacity < 2 || views == nullptr)
	{
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	for (std::uint32_t index = 0; index < 2; ++index)
	{
		views[index].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		views[index].next = nullptr;
		views[index].recommendedImageRectWidth = g_eye_width;
		views[index].maxImageRectWidth = g_eye_width;
		views[index].recommendedImageRectHeight = g_eye_height;
		views[index].maxImageRectHeight = g_eye_height;
		views[index].recommendedSwapchainSampleCount = 1;
		views[index].maxSwapchainSampleCount = 1;
	}
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEnumerateEnvironmentBlendModes(XrInstance, XrSystemId,
	const XrViewConfigurationType configuration, const std::uint32_t capacity, std::uint32_t* const count,
	XrEnvironmentBlendMode* const modes)
{
	if (configuration != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
	{
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	}
	return enumerate_one(XR_ENVIRONMENT_BLEND_MODE_OPAQUE, capacity, count, modes);
}

XrResult XRAPI_CALL mockGetD3D11GraphicsRequirementsKHR(XrInstance, XrSystemId,
	XrGraphicsRequirementsD3D11KHR* const requirements)
{
	if (requirements == nullptr)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	const std::lock_guard lock(g_mutex);
	requirements->adapterLuid = g_adapter_luid;
	requirements->minFeatureLevel = g_minimum_feature_level;
	if (g_scenario == scenario::graphics_mismatch)
	{
		++requirements->adapterLuid.LowPart;
	}
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockCreateSession(XrInstance, const XrSessionCreateInfo* const create_info,
	XrSession* const session)
{
	if (create_info == nullptr || session == nullptr || create_info->next == nullptr)
	{
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	}
	const auto* const binding = static_cast<const XrGraphicsBindingD3D11KHR*>(create_info->next);
	if (binding->type != XR_TYPE_GRAPHICS_BINDING_D3D11_KHR || binding->device == nullptr)
	{
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	}
	const std::lock_guard lock(g_mutex);
	auto* const created = new XrSession_T;
	created->id = g_next_handle++;
	created->device = binding->device;
	*session = created;
	g_current_session = created;
	++g_statistics.sessions_created;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockDestroySession(const XrSession session)
{
	if (session == XR_NULL_HANDLE)
	{
		return XR_ERROR_HANDLE_INVALID;
	}
	{
		const std::lock_guard lock(g_mutex);
		XrResult injected_result{};
		if (consume_failure_locked(failure_point::destroy_session, injected_result))
		{
			return injected_result;
		}
		if (!session->swapchains.empty() || !session->spaces.empty())
		{
			return XR_ERROR_CALL_ORDER_INVALID;
		}
		if (g_current_session == session)
		{
			g_current_session = XR_NULL_HANDLE;
		}
		++g_statistics.sessions_destroyed;
	}
	delete session;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockCreateReferenceSpace(const XrSession session, const XrReferenceSpaceCreateInfo* const create_info,
	XrSpace* const space)
{
	if (session == XR_NULL_HANDLE || create_info == nullptr || space == nullptr ||
		(create_info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL &&
			create_info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_VIEW))
	{
		return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
	}
	const std::lock_guard lock(g_mutex);
	*space = new XrSpace_T{g_next_handle++, session, create_info->referenceSpaceType};
	session->spaces.push_back(*space);
	++g_statistics.spaces_created;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockDestroySpace(const XrSpace space)
{
	if (space == XR_NULL_HANDLE)
	{
		return XR_ERROR_HANDLE_INVALID;
	}
	{
		const std::lock_guard lock(g_mutex);
		XrResult injected_result{};
		if (consume_failure_locked(failure_point::destroy_space, injected_result))
		{
			return injected_result;
		}
		if (space->owner != XR_NULL_HANDLE)
		{
			auto& spaces = space->owner->spaces;
			spaces.erase(std::remove(spaces.begin(), spaces.end(), space), spaces.end());
		}
		++g_statistics.spaces_destroyed;
	}
	delete space;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEnumerateSwapchainFormats(XrSession, const std::uint32_t capacity,
	std::uint32_t* const count, std::int64_t* const formats)
{
	return enumerate_one<std::int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, capacity, count, formats);
}

XrResult XRAPI_CALL mockCreateSwapchain(const XrSession session, const XrSwapchainCreateInfo* const create_info,
	XrSwapchain* const swapchain)
{
	if (session == XR_NULL_HANDLE || create_info == nullptr || swapchain == nullptr ||
		create_info->width == 0 || create_info->height == 0 || create_info->sampleCount == 0)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}

	auto* const created = new XrSwapchain_T;
	{
		const std::lock_guard lock(g_mutex);
		created->id = g_next_handle++;
	}
	D3D11_TEXTURE2D_DESC description{};
	description.Width = create_info->width;
	description.Height = create_info->height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = static_cast<DXGI_FORMAT>(create_info->format);
	description.SampleDesc = {create_info->sampleCount, 0};
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET;

	for (std::uint32_t index = 0; index < 2; ++index)
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		const auto result = session->device->CreateTexture2D(&description, nullptr, &texture);
		if (FAILED(result))
		{
			delete created;
			return XR_ERROR_RUNTIME_FAILURE;
		}
		created->textures.emplace_back(std::move(texture));
	}

	{
		const std::lock_guard lock(g_mutex);
		++g_statistics.swapchains_created;
		g_statistics.textures_created += created->textures.size();
	}
	created->owner = session;
	session->swapchains.push_back(created);
	*swapchain = created;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockDestroySwapchain(const XrSwapchain swapchain)
{
	if (swapchain == XR_NULL_HANDLE)
	{
		return XR_ERROR_HANDLE_INVALID;
	}
	{
		const std::lock_guard lock(g_mutex);
		if (swapchain->acquired)
		{
			++g_statistics.destroy_while_acquired_rejected;
			return XR_ERROR_CALL_ORDER_INVALID;
		}
		XrResult injected_result{};
		if (consume_failure_locked(failure_point::destroy_swapchain, injected_result))
		{
			return injected_result;
		}
		if (swapchain->owner != XR_NULL_HANDLE)
		{
			auto& swapchains = swapchain->owner->swapchains;
			swapchains.erase(std::remove(swapchains.begin(), swapchains.end(), swapchain), swapchains.end());
		}
		++g_statistics.swapchains_destroyed;
	}
	delete swapchain;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEnumerateSwapchainImages(const XrSwapchain swapchain, const std::uint32_t capacity,
	std::uint32_t* const count, XrSwapchainImageBaseHeader* const images)
{
	if (swapchain == XR_NULL_HANDLE || count == nullptr)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	{
		const std::lock_guard lock(g_mutex);
		if (g_scenario == scenario::swapchain_image_failure)
		{
			return XR_ERROR_RUNTIME_FAILURE;
		}
	}
	*count = static_cast<std::uint32_t>(swapchain->textures.size());
	if (capacity == 0)
	{
		return XR_SUCCESS;
	}
	if (capacity < swapchain->textures.size() || images == nullptr)
	{
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	auto* const d3d_images = reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
	for (std::size_t index = 0; index < swapchain->textures.size(); ++index)
	{
		if (d3d_images[index].type != XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR)
		{
			return XR_ERROR_VALIDATION_FAILURE;
		}
		d3d_images[index].texture = swapchain->textures[index].Get();
	}
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockAcquireSwapchainImage(const XrSwapchain swapchain,
	const XrSwapchainImageAcquireInfo*, std::uint32_t* const index)
{
	if (swapchain == XR_NULL_HANDLE || index == nullptr || swapchain->acquired)
	{
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	{
		const std::lock_guard lock(g_mutex);
		XrResult injected_result{};
		if (consume_failure_locked(failure_point::acquire_swapchain_image, injected_result))
		{
			return injected_result;
		}
		++g_statistics.images_acquired;
	}
	swapchain->acquired = true;
	swapchain->waited = false;
	*index = swapchain->next_image++ % static_cast<std::uint32_t>(swapchain->textures.size());
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockWaitSwapchainImage(const XrSwapchain swapchain, const XrSwapchainImageWaitInfo*)
{
	if (swapchain == XR_NULL_HANDLE || !swapchain->acquired || swapchain->waited)
	{
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	const std::lock_guard lock(g_mutex);
	if (g_scenario == scenario::wait_timeout && !g_wait_timeout_returned)
	{
		g_wait_timeout_returned = true;
		return XR_TIMEOUT_EXPIRED;
	}
	XrResult result{XR_SUCCESS};
	(void)consume_failure_locked(failure_point::wait_swapchain_image, result);
	if (result == XR_TIMEOUT_EXPIRED || XR_FAILED(result))
	{
		return result;
	}
	swapchain->waited = true;
	++g_statistics.images_waited;
	return result;
}

XrResult XRAPI_CALL mockReleaseSwapchainImage(const XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*)
{
	if (swapchain == XR_NULL_HANDLE || !swapchain->acquired || !swapchain->waited)
	{
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	{
		const std::lock_guard lock(g_mutex);
		XrResult injected_result{};
		if (consume_failure_locked(failure_point::release_swapchain_image, injected_result))
		{
			return injected_result;
		}
		++g_statistics.images_released;
	}
	swapchain->acquired = false;
	swapchain->waited = false;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockPollEvent(XrInstance, XrEventDataBuffer* const event_data)
{
	if (event_data == nullptr)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	const std::lock_guard lock(g_mutex);
	if (g_session_states.empty())
	{
		if (!g_profile_changed) return XR_EVENT_UNAVAILABLE;
		g_profile_changed = false;
		const XrEventDataInteractionProfileChanged changed{XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED, nullptr, g_current_session};
		std::memcpy(event_data, &changed, sizeof(changed));
		return XR_SUCCESS;
	}
	const XrEventDataSessionStateChanged changed{
		XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED,
		nullptr,
		g_current_session,
		g_session_states.front(),
		g_next_display_time,
	};
	if (g_current_session != XR_NULL_HANDLE) g_current_session->state = changed.state;
	g_session_states.pop_front();
	static_assert(sizeof(changed) <= sizeof(*event_data));
	std::memcpy(event_data, &changed, sizeof(changed));
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockBeginSession(const XrSession session, const XrSessionBeginInfo* const begin_info)
{
	if (session == XR_NULL_HANDLE || begin_info == nullptr || session->running ||
		begin_info->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
	{
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	session->running = true;
	const std::lock_guard lock(g_mutex);
	++g_statistics.sessions_begun;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEndSession(const XrSession session)
{
	if (session == XR_NULL_HANDLE || !session->running)
	{
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	const std::lock_guard lock(g_mutex);
	if (session->state != XR_SESSION_STATE_STOPPING)
	{
		++g_statistics.invalid_session_end_rejected;
		session->running = false; // xrEndSession transitions even on an error.
		return XR_ERROR_SESSION_NOT_STOPPING;
	}
	session->running = false;
	++g_statistics.sessions_ended;
	XrResult injected_result{};
	if (consume_failure_locked(failure_point::end_session, injected_result)) return injected_result;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockWaitFrame(const XrSession session, const XrFrameWaitInfo*, XrFrameState* const state)
{
	if (session == XR_NULL_HANDLE || !session->running || state == nullptr)
	{
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	if (session->frame_waited || session->frame_begun)
	{
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	session->frame_waited = true;
	const std::lock_guard lock(g_mutex);
	state->predictedDisplayTime = g_next_display_time++;
	session->display_time=state->predictedDisplayTime;
	state->predictedDisplayPeriod = 1;
	state->shouldRender = g_should_render;
	++g_statistics.frames_waited;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockBeginFrame(const XrSession session, const XrFrameBeginInfo*)
{
	if (session == XR_NULL_HANDLE || !session->running)
	{
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	if (!session->frame_waited || session->frame_begun)
	{
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	session->frame_waited = false;
	session->frame_begun = true;
	const std::lock_guard lock(g_mutex);
	++g_statistics.frames_begun;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockLocateViews(const XrSession session, const XrViewLocateInfo* const locate_info,
	XrViewState* const view_state, const std::uint32_t capacity, std::uint32_t* const count, XrView* const views)
{
	if (session == XR_NULL_HANDLE || !session->running || locate_info == nullptr || view_state == nullptr ||
		count == nullptr || capacity < 2 || views == nullptr || locate_info->displayTime!=session->display_time)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	{
		const std::lock_guard lock(g_mutex);
		XrResult result{};
		if (consume_failure_locked(failure_point::locate_views, result)) return result;
		view_state->viewStateFlags = g_view_flags;
	}
	if (g_scenario == scenario::view_count_mismatch) { *count = 1; return XR_SUCCESS; }
	*count = 2;
	for (std::uint32_t index = 0; index < 2; ++index)
	{
		views[index].type = XR_TYPE_VIEW;
		views[index].next = nullptr;
		views[index].pose = {};
		views[index].pose.orientation.w = 1.0f;
		if (g_scenario == scenario::invalid_eye_pose && index == 1)
			views[index].pose.orientation.w = 0;
		if(g_scenario==scenario::canted_views){views[index].pose.orientation.y=index==0?-.05f:.05f;views[index].pose.orientation.w=std::sqrt(1-.05f*.05f);}
		views[index].pose.position.x = index == 0 ? -0.032f : 0.032f;
		views[index].pose.position.y = g_head_height;
		if (g_scenario == scenario::parallel_views)
		{
			// One coherent eye sample, different from the separately located VIEW space.
			views[index].pose.orientation = {0, std::sin(.1f), 0, std::cos(.1f)};
			const float offset = index == 0 ? -.032f : .032f;
			views[index].pose.position = {.1f + offset * std::cos(.2f), g_head_height + .02f,
			                              -.03f - offset * std::sin(.2f)};
		}
		if (g_scenario == scenario::scaled_view_quaternions)
		{
			// Equivalent rotations: opposite signs and small accepted length error.
			views[index].pose.orientation.w = index == 0 ? .999f : -1.001f;
		}
		views[index].fov = {-0.7f, 0.7f, 0.7f, -0.7f};
		session->located_views[index] = views[index];
	}
	const std::lock_guard lock(g_mutex);
	++g_statistics.views_located;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEndFrame(XrSession session,const XrFrameEndInfo* end)
{
	if(!session||!session->running||!session->frame_begun||!end||end->displayTime!=session->display_time||end->layerCount>8||(end->layerCount&&!end->layers))return XR_ERROR_CALL_ORDER_INVALID;
	session->frame_begun=false;
	bool projection=false,quad=false,cylinder=false;
	for(unsigned layer_index=0;layer_index<end->layerCount;++layer_index)
	{
		const auto* layer=end->layers[layer_index];if(!layer)return XR_ERROR_LAYER_INVALID;
		if(layer->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION)
		{
			const auto* p=reinterpret_cast<const XrCompositionLayerProjection*>(layer);
			if(p->viewCount!=2||!p->views)return XR_ERROR_VALIDATION_FAILURE;
			std::array<std::array<std::uint8_t,4>,2> pixels;
			for(unsigned eye=0;eye<2;++eye)
			{
				const auto& view=p->views[eye];
				const auto& expected = session->located_views[eye].pose;
				if(view.pose.orientation.x!=expected.orientation.x || view.pose.orientation.y!=expected.orientation.y ||
					view.pose.orientation.z!=expected.orientation.z || view.pose.orientation.w!=expected.orientation.w ||
					view.pose.position.x!=expected.position.x || view.pose.position.y!=expected.position.y || view.pose.position.z!=expected.position.z ||
					view.fov.angleLeft!=-.7f || view.fov.angleRight!=.7f || view.fov.angleUp!=.7f || view.fov.angleDown!=-.7f ||
					view.type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW||!view.subImage.swapchain||view.subImage.imageArrayIndex||
					view.subImage.imageRect.extent.width!=int(g_eye_width)||view.subImage.imageRect.extent.height!=int(g_eye_height)||
					!read_first_pixel(session,view.subImage.swapchain,pixels[eye]))return XR_ERROR_VALIDATION_FAILURE;
				std::memcpy(&g_statistics.last_projection_pixels[eye],pixels[eye].data(),4);
			}
			if(g_synthetic_checks)
			{
				if(!(pixels[0][0]>200&&pixels[0][1]<160&&pixels[0][2]>200&&pixels[1][0]<160&&pixels[1][1]>200&&pixels[1][2]>200)||
					pixels[0]==pixels[1]||!pixels[0][3]||pixels[0][3]!=pixels[1][3]||(g_has_texture_epoch&&pixels[0][3]==g_last_texture_epochs[0]))
					return XR_ERROR_VALIDATION_FAILURE;
				g_last_texture_epochs={pixels[0][3],pixels[1][3]};g_has_texture_epoch=true;++g_statistics.texture_write_epochs_validated;
			}
			projection=true;
		}
		else if(layer->type==XR_TYPE_COMPOSITION_LAYER_QUAD || layer->type==XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR)
		{
			const auto sub=layer->type==XR_TYPE_COMPOSITION_LAYER_QUAD?reinterpret_cast<const XrCompositionLayerQuad*>(layer)->subImage:
				reinterpret_cast<const XrCompositionLayerCylinderKHR*>(layer)->subImage;
			std::array<std::uint8_t,4> pixel;
			if(!sub.swapchain||sub.imageArrayIndex)return XR_ERROR_SWAPCHAIN_RECT_INVALID;
			D3D11_TEXTURE2D_DESC description;sub.swapchain->textures[0]->GetDesc(&description);
			if(sub.imageRect.extent.width>int(description.Width)||sub.imageRect.extent.height>int(description.Height))return XR_ERROR_SWAPCHAIN_RECT_INVALID;
			if(!read_first_pixel(session,sub.swapchain,pixel))return XR_ERROR_LAYER_INVALID;
			std::memcpy(&g_statistics.last_menu_pixel,pixel.data(),4);
			quad|=layer->type==XR_TYPE_COMPOSITION_LAYER_QUAD;cylinder|=layer->type==XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
		}
		else return XR_ERROR_LAYER_INVALID;
	}
	const std::lock_guard lock(g_mutex);++g_statistics.frames_ended;
	if(!end->layerCount)++g_statistics.zero_layer_frames;
	if(projection){++g_statistics.projection_frames;++g_statistics.eye_color_frames_validated;}
	if(quad)++g_statistics.quad_frames;if(cylinder)++g_statistics.cylinder_frames;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockStringToPath(XrInstance,const char* text,XrPath* output)
{
	if(!text||!output||text[0]!='/')return XR_ERROR_PATH_FORMAT_INVALID;
	const std::lock_guard lock(g_mutex);
	for(const auto& pair:g_paths)if(pair.second==text){*output=pair.first;return XR_SUCCESS;}
	*output=g_next_handle++;g_paths[*output]=text;return XR_SUCCESS;
}
XrResult XRAPI_CALL mockCreateActionSet(XrInstance instance,const XrActionSetCreateInfo* info,XrActionSet* output)
{
	if(!instance||!info||!output||!info->actionSetName[0])return XR_ERROR_VALIDATION_FAILURE;
	const std::lock_guard lock(g_mutex);*output=new XrActionSet_T;(*output)->owner=instance;
	++g_statistics.action_sets_created;return XR_SUCCESS;
}
XrResult XRAPI_CALL mockCreateAction(XrActionSet set,const XrActionCreateInfo* info,XrAction* output)
{
	if(!set||!info||!output||!info->actionName[0])return XR_ERROR_VALIDATION_FAILURE;
	const std::lock_guard lock(g_mutex);*output=new XrAction_T{set,info->actionName,info->actionType,false};
	set->actions.push_back(*output);++g_statistics.actions_created;return XR_SUCCESS;
}
XrResult XRAPI_CALL mockDestroyActionSet(XrActionSet set)
{
	if(!set)return XR_ERROR_HANDLE_INVALID;
	const std::lock_guard lock(g_mutex);
	if(g_current_session&&g_current_session->actions==set)g_current_session->actions=XR_NULL_HANDLE;
	for(auto action:set->actions){delete action;++g_statistics.actions_destroyed;}
	delete set;++g_statistics.action_sets_destroyed;return XR_SUCCESS;
}
XrResult XRAPI_CALL mockSuggestInteractionProfileBindings(XrInstance,const XrInteractionProfileSuggestedBinding* info)
{
	if(!info||!info->suggestedBindings||!info->countSuggestedBindings)return XR_ERROR_VALIDATION_FAILURE;
	const std::lock_guard lock(g_mutex);
	if(!g_paths.contains(info->interactionProfile))return XR_ERROR_PATH_INVALID;
	for(unsigned i=0;i<info->countSuggestedBindings;++i)
	{
		const auto& b=info->suggestedBindings[i];if(!b.action||!g_paths.contains(b.binding))return XR_ERROR_PATH_INVALID;
		if(g_paths[info->interactionProfile]=="/interaction_profiles/oculus/touch_controller")b.action->bound=true;
	}
	++g_statistics.binding_profiles;return XR_SUCCESS;
}
XrResult XRAPI_CALL mockAttachSessionActionSets(XrSession session,const XrSessionActionSetsAttachInfo* info)
{
	if(!session||!info||info->countActionSets!=1||!info->actionSets)return XR_ERROR_VALIDATION_FAILURE;
	if(session->actions)return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	session->actions=info->actionSets[0];return XR_SUCCESS;
}
XrResult XRAPI_CALL mockCreateActionSpace(XrSession session,const XrActionSpaceCreateInfo* info,XrSpace* output)
{
	if(!session||!info||!output||!info->action||info->action->type!=XR_ACTION_TYPE_POSE_INPUT)return XR_ERROR_VALIDATION_FAILURE;
	const std::lock_guard lock(g_mutex);*output=new XrSpace_T{g_next_handle++,session,XR_REFERENCE_SPACE_TYPE_LOCAL,info->action};
	session->spaces.push_back(*output);++g_statistics.spaces_created;return XR_SUCCESS;
}
XrResult XRAPI_CALL mockSyncActions(XrSession session,const XrActionsSyncInfo* info)
{
	if(!session||!session->running||!info||info->countActiveActionSets!=1||!session->actions)return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	const std::lock_guard lock(g_mutex);++g_statistics.action_syncs;
	session->synced=session->state==XR_SESSION_STATE_FOCUSED;
	return session->synced?XR_SUCCESS:XR_SESSION_NOT_FOCUSED;
}
XrResult XRAPI_CALL mockGetActionStateBoolean(XrSession session,const XrActionStateGetInfo* info,XrActionStateBoolean* value)
{
	if(!session||!info||!value||!info->action||info->action->type!=XR_ACTION_TYPE_BOOLEAN_INPUT)return XR_ERROR_ACTION_TYPE_MISMATCH;
	const std::lock_guard lock(g_mutex);value->isActive=session->synced&&info->action->bound;
	value->currentState=value->isActive&&g_action_values[info->action->name][0]>.5f;return XR_SUCCESS;
}
XrResult XRAPI_CALL mockGetActionStateFloat(XrSession session,const XrActionStateGetInfo* info,XrActionStateFloat* value)
{
	if(!session||!info||!value||!info->action||info->action->type!=XR_ACTION_TYPE_FLOAT_INPUT)return XR_ERROR_ACTION_TYPE_MISMATCH;
	const std::lock_guard lock(g_mutex);value->isActive=session->synced&&info->action->bound;
	value->currentState=value->isActive?g_action_values[info->action->name][0]:0;return XR_SUCCESS;
}
XrResult XRAPI_CALL mockGetActionStateVector2f(XrSession session,const XrActionStateGetInfo* info,XrActionStateVector2f* value)
{
	if(!session||!info||!value||!info->action||info->action->type!=XR_ACTION_TYPE_VECTOR2F_INPUT)return XR_ERROR_ACTION_TYPE_MISMATCH;
	const std::lock_guard lock(g_mutex);value->isActive=session->synced&&info->action->bound;
	const auto& v=g_action_values[info->action->name];value->currentState=value->isActive?XrVector2f{v[0],v[1]}:XrVector2f{};return XR_SUCCESS;
}
XrResult XRAPI_CALL mockGetActionStatePose(XrSession session,const XrActionStateGetInfo* info,XrActionStatePose* value)
{
	if(!session||!info||!value||!info->action||info->action->type!=XR_ACTION_TYPE_POSE_INPUT)return XR_ERROR_ACTION_TYPE_MISMATCH;
	value->isActive=session->synced&&info->action->bound;return XR_SUCCESS;
}
XrResult XRAPI_CALL mockLocateSpace(XrSpace space,XrSpace base,XrTime time,XrSpaceLocation* value)
{
	if(!space||!base||!value||time<=0||space->owner!=base->owner)return XR_ERROR_VALIDATION_FAILURE;
	const std::lock_guard lock(g_mutex);value->locationFlags=g_view_flags;
	value->pose={};value->pose.orientation.w=1;
	if(space->action)
	{
		if(!space->owner->synced){value->locationFlags=0;return XR_SUCCESS;}
		value->locationFlags = g_hand_flags[space->action->name.starts_with("left_") ? 0 : 1];
		value->pose.position={space->action->name.starts_with("left_")?-.2f:.2f,0,-.3f};
	}
	else value->pose.position.y = g_head_height;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockGetCurrentInteractionProfile(XrSession session, XrPath user, XrInteractionProfileState* output)
{
	if (!session || !output) return XR_ERROR_VALIDATION_FAILURE;
	const std::lock_guard lock(g_mutex);
	if (!g_paths.contains(user)) return XR_ERROR_PATH_INVALID;
	XrResult result{};
	if (consume_failure_locked(failure_point::get_current_interaction_profile,result)) return result;
	const unsigned hand = g_paths[user] == "/user/hand/left" ? 0 : 1;
	output->interactionProfile = XR_NULL_PATH;
	for (const auto& pair : g_paths)
		if (pair.second == g_profiles[hand]) output->interactionProfile = pair.first;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockApplyHapticFeedback(XrSession session,const XrHapticActionInfo* info,const XrHapticBaseHeader* value)
{
	if(!session||!info||!value||!info->action||info->action->type!=XR_ACTION_TYPE_VIBRATION_OUTPUT)return XR_ERROR_ACTION_TYPE_MISMATCH;
	if(session->state!=XR_SESSION_STATE_FOCUSED)return XR_SESSION_NOT_FOCUSED;
	const auto* pulse=reinterpret_cast<const XrHapticVibration*>(value);
	if(pulse->type!=XR_TYPE_HAPTIC_VIBRATION||pulse->duration<=0||pulse->duration>100'000'001||pulse->amplitude<=0||pulse->amplitude>1)
		return XR_ERROR_VALIDATION_FAILURE;
	const std::lock_guard lock(g_mutex);++g_statistics.haptic_events;return XR_SUCCESS;
}

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrGetInstanceProcAddr(
	XrInstance, const char* const name, PFN_xrVoidFunction* const function)
{
	if (name == nullptr || function == nullptr)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	*function = nullptr;
	{
		const std::lock_guard lock(g_mutex);
		if (g_scenario == scenario::destroy_instance_proc_null &&
			std::strcmp(name, "xrDestroyInstance") == 0)
		{
			return XR_SUCCESS;
		}
	}

#define H2V_MOCK_PROC(openxr_name, implementation) \
	if (std::strcmp(name, openxr_name) == 0) { assign_proc(function, implementation); return XR_SUCCESS; }

	H2V_MOCK_PROC("xrEnumerateInstanceExtensionProperties", mockEnumerateInstanceExtensionProperties)
	H2V_MOCK_PROC("xrEnumerateInstanceVersion", mockEnumerateInstanceVersion)
	H2V_MOCK_PROC("xrCreateInstance", mockCreateInstance)
	H2V_MOCK_PROC("xrDestroyInstance", mockDestroyInstance)
	H2V_MOCK_PROC("xrGetInstanceProperties", mockGetInstanceProperties)
	H2V_MOCK_PROC("xrGetSystem", mockGetSystem)
	H2V_MOCK_PROC("xrGetSystemProperties", mockGetSystemProperties)
	H2V_MOCK_PROC("xrEnumerateViewConfigurations", mockEnumerateViewConfigurations)
	H2V_MOCK_PROC("xrEnumerateViewConfigurationViews", mockEnumerateViewConfigurationViews)
	H2V_MOCK_PROC("xrEnumerateEnvironmentBlendModes", mockEnumerateEnvironmentBlendModes)
	H2V_MOCK_PROC("xrGetD3D11GraphicsRequirementsKHR", mockGetD3D11GraphicsRequirementsKHR)
	H2V_MOCK_PROC("xrCreateSession", mockCreateSession)
	H2V_MOCK_PROC("xrDestroySession", mockDestroySession)
	H2V_MOCK_PROC("xrCreateReferenceSpace", mockCreateReferenceSpace)
	H2V_MOCK_PROC("xrDestroySpace", mockDestroySpace)
	H2V_MOCK_PROC("xrEnumerateSwapchainFormats", mockEnumerateSwapchainFormats)
	H2V_MOCK_PROC("xrCreateSwapchain", mockCreateSwapchain)
	H2V_MOCK_PROC("xrDestroySwapchain", mockDestroySwapchain)
	H2V_MOCK_PROC("xrEnumerateSwapchainImages", mockEnumerateSwapchainImages)
	H2V_MOCK_PROC("xrAcquireSwapchainImage", mockAcquireSwapchainImage)
	H2V_MOCK_PROC("xrWaitSwapchainImage", mockWaitSwapchainImage)
	H2V_MOCK_PROC("xrReleaseSwapchainImage", mockReleaseSwapchainImage)
	H2V_MOCK_PROC("xrPollEvent", mockPollEvent)
	H2V_MOCK_PROC("xrBeginSession", mockBeginSession)
	H2V_MOCK_PROC("xrEndSession", mockEndSession)
	H2V_MOCK_PROC("xrWaitFrame", mockWaitFrame)
	H2V_MOCK_PROC("xrBeginFrame", mockBeginFrame)
	H2V_MOCK_PROC("xrLocateViews", mockLocateViews)
	H2V_MOCK_PROC("xrEndFrame", mockEndFrame)
	H2V_MOCK_PROC("xrStringToPath", mockStringToPath)
	H2V_MOCK_PROC("xrCreateActionSet", mockCreateActionSet)
	H2V_MOCK_PROC("xrDestroyActionSet", mockDestroyActionSet)
	H2V_MOCK_PROC("xrCreateAction", mockCreateAction)
	H2V_MOCK_PROC("xrSuggestInteractionProfileBindings", mockSuggestInteractionProfileBindings)
	H2V_MOCK_PROC("xrAttachSessionActionSets", mockAttachSessionActionSets)
	H2V_MOCK_PROC("xrCreateActionSpace", mockCreateActionSpace)
	H2V_MOCK_PROC("xrLocateSpace", mockLocateSpace)
	H2V_MOCK_PROC("xrSyncActions", mockSyncActions)
	H2V_MOCK_PROC("xrGetCurrentInteractionProfile", mockGetCurrentInteractionProfile)
	H2V_MOCK_PROC("xrGetActionStateBoolean", mockGetActionStateBoolean)
	H2V_MOCK_PROC("xrGetActionStateFloat", mockGetActionStateFloat)
	H2V_MOCK_PROC("xrGetActionStateVector2f", mockGetActionStateVector2f)
	H2V_MOCK_PROC("xrGetActionStatePose", mockGetActionStatePose)
	H2V_MOCK_PROC("xrApplyHapticFeedback", mockApplyHapticFeedback)

#undef H2V_MOCK_PROC
	return XR_ERROR_FUNCTION_UNSUPPORTED;
}

// Allows the manifest-integration smoke test to verify exact export-driven
// binding without loading or scanning a real SteamVR installation.
extern "C" __declspec(dllexport) void* VRClientCoreFactory(const char*, int*)
{
	return nullptr;
}
