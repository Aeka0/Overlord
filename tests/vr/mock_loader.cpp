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

struct XrInstance_T
{
	std::uint64_t id{};
};

struct XrSession_T
{
	std::uint64_t id{};
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	std::vector<XrSwapchain> swapchains;
	std::vector<XrSpace> spaces;
	bool running{};
	bool frame_waited{};
	bool frame_begun{};
};

struct XrSpace_T
{
	std::uint64_t id{};
	XrSession owner{XR_NULL_HANDLE};
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
	bool g_wait_timeout_returned{};
	std::deque<XrSessionState> g_session_states;
	statistics g_statistics;
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
	g_wait_timeout_returned = false;
	g_session_states.clear();
	g_statistics = {};
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
	*count = 1;
	if (capacity == 0)
	{
		return XR_SUCCESS;
	}
	if (capacity < 1 || properties == nullptr)
	{
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	properties[0].type = XR_TYPE_EXTENSION_PROPERTIES;
	properties[0].next = nullptr;
	strcpy_s(properties[0].extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
	properties[0].extensionVersion = XR_KHR_D3D11_enable_SPEC_VERSION;
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
	if (create_info == nullptr || instance == nullptr || create_info->enabledExtensionCount != 1 ||
		create_info->enabledExtensionNames == nullptr ||
		std::strcmp(create_info->enabledExtensionNames[0], XR_KHR_D3D11_ENABLE_EXTENSION_NAME) != 0)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	const std::lock_guard lock(g_mutex);
	*instance = new XrInstance_T{g_next_handle++};
	++g_statistics.instances_created;
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
	strcpy_s(properties->runtimeName, "h2v mock OpenXR runtime");
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
		views[index].recommendedImageRectWidth = 64;
		views[index].maxImageRectWidth = 64;
		views[index].recommendedImageRectHeight = 64;
		views[index].maxImageRectHeight = 64;
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
	*space = new XrSpace_T{g_next_handle++, session};
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
		return XR_EVENT_UNAVAILABLE;
	}
	const XrEventDataSessionStateChanged changed{
		XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED,
		nullptr,
		g_current_session,
		g_session_states.front(),
		g_next_display_time,
	};
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
	session->running = false;
	const std::lock_guard lock(g_mutex);
	++g_statistics.sessions_ended;
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
		count == nullptr || capacity < 2 || views == nullptr)
	{
		return XR_ERROR_VALIDATION_FAILURE;
	}
	view_state->viewStateFlags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
	*count = 2;
	for (std::uint32_t index = 0; index < 2; ++index)
	{
		views[index].type = XR_TYPE_VIEW;
		views[index].next = nullptr;
		views[index].pose = {};
		views[index].pose.orientation.w = 1.0f;
		views[index].pose.position.x = index == 0 ? -0.032f : 0.032f;
		views[index].fov = {-0.7f, 0.7f, 0.7f, -0.7f};
	}
	const std::lock_guard lock(g_mutex);
	++g_statistics.views_located;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEndFrame(const XrSession session, const XrFrameEndInfo* const end_info)
{
	if (session == XR_NULL_HANDLE || !session->running || !session->frame_begun || end_info == nullptr ||
		end_info->layerCount > 1 || (end_info->layerCount != 0 && end_info->layers == nullptr))
	{
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	session->frame_begun = false;
	if (end_info->layerCount != 0)
	{
		const auto* const projection = reinterpret_cast<const XrCompositionLayerProjection*>(end_info->layers[0]);
		if (projection == nullptr || projection->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION ||
			projection->viewCount != 2 || projection->views == nullptr)
		{
			return XR_ERROR_VALIDATION_FAILURE;
		}
		std::array<std::array<std::uint8_t, 4>, 2> pixels{};
		for (std::size_t index = 0; index < pixels.size(); ++index)
		{
			const auto& view = projection->views[index];
			if (view.type != XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW ||
				view.subImage.swapchain == XR_NULL_HANDLE || view.subImage.imageArrayIndex != 0 ||
				view.subImage.imageRect.offset.x != 0 || view.subImage.imageRect.offset.y != 0 ||
				view.subImage.imageRect.extent.width != 64 || view.subImage.imageRect.extent.height != 64 ||
				!read_first_pixel(session, view.subImage.swapchain, pixels[index]))
			{
				return XR_ERROR_VALIDATION_FAILURE;
			}
		}
		const bool left_magenta = pixels[0][0] > 200 && pixels[0][1] < 160 && pixels[0][2] > 200;
		const bool right_cyan = pixels[1][0] < 160 && pixels[1][1] > 200 && pixels[1][2] > 200;
		if (!left_magenta || !right_cyan || pixels[0] == pixels[1] || pixels[0][3] == 0 ||
			pixels[0][3] != pixels[1][3])
		{
			return XR_ERROR_VALIDATION_FAILURE;
		}
		const std::lock_guard lock(g_mutex);
		if (g_has_texture_epoch && pixels[0][3] == g_last_texture_epochs[0])
		{
			return XR_ERROR_VALIDATION_FAILURE;
		}
		g_last_texture_epochs = {pixels[0][3], pixels[1][3]};
		g_has_texture_epoch = true;
		++g_statistics.texture_write_epochs_validated;
	}
	const std::lock_guard lock(g_mutex);
	++g_statistics.frames_ended;
	if (end_info->layerCount == 0)
	{
		++g_statistics.zero_layer_frames;
	}
	else
	{
		++g_statistics.projection_frames;
		++g_statistics.eye_color_frames_validated;
	}
	return XR_SUCCESS;
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

#undef H2V_MOCK_PROC
	return XR_ERROR_FUNCTION_UNSUPPORTED;
}

// Allows the manifest-integration smoke test to verify exact export-driven
// binding without loading or scanning a real SteamVR installation.
extern "C" __declspec(dllexport) void* VRClientCoreFactory(const char*, int*)
{
	return nullptr;
}
