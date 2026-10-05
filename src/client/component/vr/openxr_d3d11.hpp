#pragma once

#include "openxr_dispatch.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "component/d3d11.hpp"

#if H2V_OPENXR_HEADERS_AVAILABLE
#include <wrl/client.h>

namespace vr::openxr
{
	struct eye_swapchain
	{
		XrSwapchain handle{XR_NULL_HANDLE};
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t sample_count{1};
		std::vector<XrSwapchainImageD3D11KHR> images;
		std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> render_targets;
		bool acquired{};
		bool waited{};
		std::uint32_t acquired_index{};
	};

	[[nodiscard]] bool get_device_adapter_luid(ID3D11Device* device, LUID& luid, std::string& error);
	[[nodiscard]] bool create_private_device(const XrGraphicsRequirementsD3D11KHR& requirements,
		d3d11::device_snapshot& output, std::string& error);
	[[nodiscard]] bool graphics_requirements_match(const XrGraphicsRequirementsD3D11KHR& requirements,
		const d3d11::device_snapshot& graphics, std::string& error);
	[[nodiscard]] bool wait_for_gpu_idle(ID3D11Device* device, ID3D11DeviceContext* context,
		std::string& error) noexcept;
	[[nodiscard]] bool create_eye_swapchain(const dispatch_table& dispatch, XrSession session,
		ID3D11Device* device, std::int64_t format, const XrViewConfigurationView& view, eye_swapchain& output,
		std::string& error, XrResult& last_result);
	[[nodiscard]] bool release_acquired_image(const dispatch_table& dispatch, eye_swapchain& eye,
		XrResult& last_result) noexcept;
	[[nodiscard]] bool destroy_eye_swapchain(const dispatch_table& dispatch, eye_swapchain& eye,
		XrResult& last_result, bool destroy_handle = true) noexcept;
}
#endif
