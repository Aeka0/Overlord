#include <std_include.hpp>

#include "openxr_d3d11.hpp"

#include <chrono>
#include <format>
#include <dxgi1_6.h>

#if H2V_OPENXR_HEADERS_AVAILABLE
namespace vr::openxr
{
	namespace
	{
		bool luid_equal(const LUID& left, const LUID& right) noexcept
		{
			return left.HighPart == right.HighPart && left.LowPart == right.LowPart;
		}

		std::uint32_t valid_sample_count(const std::uint32_t recommended) noexcept
		{
			return recommended == 0 ? 1 : recommended;
		}
	}

	bool get_device_adapter_luid(ID3D11Device* const device, LUID& luid, std::string& error)
	{
		if (device == nullptr)
		{
			error = "D3D11 device is unavailable";
			return false;
		}
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		DXGI_ADAPTER_DESC description{};
		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) ||
		    FAILED(dxgi_device->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&description)))
		{
			error = "Failed to query the D3D11 device adapter LUID";
			return false;
		}
		luid = description.AdapterLuid;
		return true;
	}

	bool create_private_device(const XrGraphicsRequirementsD3D11KHR& requirements,
	                           d3d11::device_snapshot& output,
	                           std::string& error)
	{
		output = {};
		Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		{
			error = "Failed to create DXGI factory for OpenXR private device";
			return false;
		}
		Microsoft::WRL::ComPtr<IDXGIAdapter1> selected;
		for (UINT index = 0;; ++index)
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
			const auto result = factory->EnumAdapters1(index, &adapter);
			if (result == DXGI_ERROR_NOT_FOUND)
				break;
			if (FAILED(result))
				continue;
			DXGI_ADAPTER_DESC1 description{};
			if (FAILED(adapter->GetDesc1(&description)))
				continue;
			if (luid_equal(description.AdapterLuid, requirements.adapterLuid))
			{
				selected = std::move(adapter);
				break;
			}
		}
		if (!selected)
		{
			error = "OpenXR graphics adapter LUID was not found";
			return false;
		}

		using create_device_fn = HRESULT(WINAPI*)(IDXGIAdapter*,
		                                          D3D_DRIVER_TYPE,
		                                          HMODULE,
		                                          UINT,
		                                          const D3D_FEATURE_LEVEL*,
		                                          UINT,
		                                          UINT,
		                                          ID3D11Device**,
		                                          D3D_FEATURE_LEVEL*,
		                                          ID3D11DeviceContext**);
		const auto d3d11_module = GetModuleHandleW(L"d3d11.dll");
		const auto create_device =
		    d3d11_module == nullptr
		        ? nullptr
		        : reinterpret_cast<create_device_fn>(GetProcAddress(d3d11_module, "D3D11CreateDevice"));
		if (create_device == nullptr)
		{
			error = "D3D11CreateDevice export is unavailable for OpenXR private device";
			return false;
		}
		const D3D_FEATURE_LEVEL levels[]{
		    D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
		D3D_FEATURE_LEVEL feature_level{};
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		const auto result = create_device(selected.Get(),
		                                  D3D_DRIVER_TYPE_UNKNOWN,
		                                  nullptr,
		                                  0,
		                                  levels,
		                                  static_cast<UINT>(std::size(levels)),
		                                  D3D11_SDK_VERSION,
		                                  &device,
		                                  &feature_level,
		                                  &context);
		if (FAILED(result) || !device || !context || feature_level < requirements.minFeatureLevel)
		{
			error =
			    std::format("Failed to create OpenXR private D3D11 device (HRESULT=0x{:08x}, feature=0x{:x})",
			                static_cast<std::uint32_t>(result),
			                static_cast<unsigned int>(feature_level));
			return false;
		}
		output.device = std::move(device);
		output.context = std::move(context);
		output.feature_level = feature_level;
		output.generation = 1;
		return true;
	}

	bool graphics_requirements_match(const XrGraphicsRequirementsD3D11KHR& requirements,
	                                 const d3d11::device_snapshot& graphics,
	                                 std::string& error)
	{
		LUID game_luid{};
		if (!graphics || !get_device_adapter_luid(graphics.device.Get(), game_luid, error))
			return false;
		if (!luid_equal(requirements.adapterLuid, game_luid))
		{
			error = std::format(
			    "OpenXR graphics adapter mismatch: game LUID={:08x}:{:08x}, runtime LUID={:08x}:{:08x}; game feature level=0x{:x}, runtime minimum feature level=0x{:x}",
			    static_cast<std::uint32_t>(game_luid.HighPart),
			    game_luid.LowPart,
			    static_cast<std::uint32_t>(requirements.adapterLuid.HighPart),
			    requirements.adapterLuid.LowPart,
			    static_cast<unsigned int>(graphics.feature_level),
			    static_cast<unsigned int>(requirements.minFeatureLevel));
			return false;
		}
		if (graphics.feature_level < requirements.minFeatureLevel)
		{
			error = "Game D3D11 feature level is below the OpenXR minimum";
			return false;
		}
		return true;
	}

	bool wait_for_gpu_idle(ID3D11Device* const device,
	                       ID3D11DeviceContext* const context,
	                       std::string& error) noexcept
	{
		if (device == nullptr || context == nullptr)
		{
			error = "D3D11 device/context is unavailable during OpenXR teardown";
			return false;
		}
		const D3D11_QUERY_DESC query_description{D3D11_QUERY_EVENT, 0};
		Microsoft::WRL::ComPtr<ID3D11Query> completion_query;
		if (FAILED(device->CreateQuery(&query_description, &completion_query)))
		{
			error = "Failed to create D3D11 OpenXR completion query";
			return false;
		}
		context->End(completion_query.Get());
		context->Flush();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		HRESULT result{};
		while ((result = context->GetData(
		            completion_query.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE)
		{
			if (FAILED(device->GetDeviceRemovedReason()))
				return true;
			if (std::chrono::steady_clock::now() >= deadline)
			{
				error = "Timed out waiting for D3D11 commands referencing OpenXR swapchains";
				return false;
			}
			Sleep(0);
		}
		if (FAILED(result))
		{
			error = std::format("D3D11 OpenXR completion wait failed (HRESULT=0x{:08x})",
			                    static_cast<std::uint32_t>(result));
			return false;
		}
		return true;
	}

	bool create_eye_swapchain(const dispatch_table& dispatch,
	                          const XrSession session,
	                          ID3D11Device* const device,
	                          const std::int64_t format,
	                          const XrViewConfigurationView& view,
	                          eye_swapchain& output,
	                          std::string& error,
	                          XrResult& last_result)
	{
		output = {};
		output.width = view.recommendedImageRectWidth;
		output.height = view.recommendedImageRectHeight;
		output.sample_count = 1; // H2 and the transport ring use single-sample color.
		if (output.width == 0 || output.height == 0 || output.width > 8192 || output.height > 8192 ||
		    std::uint64_t(output.width) * output.height > 33554432)
		{
			error = "OpenXR returned invalid recommended eye dimensions";
			return false;
		}
		const XrSwapchainCreateInfo create_info{XR_TYPE_SWAPCHAIN_CREATE_INFO,
		                                        nullptr,
		                                        0,
		                                        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
		                                        format,
		                                        output.sample_count,
		                                        output.width,
		                                        output.height,
		                                        1,
		                                        1,
		                                        1};
		last_result = dispatch.create_swapchain(session, &create_info, &output.handle);
		if (XR_FAILED(last_result))
		{
			error =
			    std::format("xrCreateSwapchain failed (XrResult={})", static_cast<std::int64_t>(last_result));
			return false;
		}
		std::uint32_t image_count{};
		last_result = dispatch.enumerate_swapchain_images(output.handle, 0, &image_count, nullptr);
		if (XR_FAILED(last_result) || image_count == 0 || image_count > 64)
		{
			error = "xrEnumerateSwapchainImages(count) failed";
			XrResult cleanup{XR_SUCCESS};
			(void)destroy_eye_swapchain(dispatch, output, cleanup);
			return false;
		}
		output.images.resize(image_count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
		last_result = dispatch.enumerate_swapchain_images(
		    output.handle,
		    image_count,
		    &image_count,
		    reinterpret_cast<XrSwapchainImageBaseHeader*>(output.images.data()));
		if (XR_FAILED(last_result))
		{
			error = "xrEnumerateSwapchainImages failed";
			XrResult cleanup{XR_SUCCESS};
			(void)destroy_eye_swapchain(dispatch, output, cleanup);
			return false;
		}
		D3D11_RENDER_TARGET_VIEW_DESC view_description{};
		view_description.Format = static_cast<DXGI_FORMAT>(format);
		view_description.ViewDimension =
		    output.sample_count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
		for (const auto& image : output.images)
		{
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target;
			if (FAILED(device->CreateRenderTargetView(image.texture, &view_description, &render_target)))
			{
				error = "CreateRenderTargetView for OpenXR image failed";
				XrResult cleanup{XR_SUCCESS};
				(void)destroy_eye_swapchain(dispatch, output, cleanup);
				return false;
			}
			output.render_targets.emplace_back(std::move(render_target));
		}
		return true;
	}

	bool release_acquired_image(const dispatch_table& dispatch,
	                            eye_swapchain& eye,
	                            XrResult& last_result) noexcept
	{
		if (!eye.acquired || !eye.waited || eye.handle == XR_NULL_HANDLE)
			return false;
		const XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		last_result = dispatch.release_swapchain_image(eye.handle, &release_info);
		if (XR_FAILED(last_result))
			return false;
		eye.acquired = false;
		eye.waited = false;
		return true;
	}

	bool destroy_eye_swapchain(const dispatch_table& dispatch,
	                           eye_swapchain& eye,
	                           XrResult& last_result,
	                           const bool destroy_handle) noexcept
	{
		if (!destroy_handle)
			return false;
		if (eye.handle != XR_NULL_HANDLE)
		{
			if (dispatch.destroy_swapchain == nullptr)
			{
				last_result = XR_ERROR_FUNCTION_UNSUPPORTED;
				return false;
			}
			eye.render_targets.clear();
			eye.images.clear();
			last_result = dispatch.destroy_swapchain(eye.handle);
			if (XR_FAILED(last_result))
				return false;
		}
		eye = {};
		return true;
	}
}
#endif
