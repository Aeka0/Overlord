#pragma once

#include <std_include.hpp>

#include "component/d3d11.hpp"

namespace vr::tests
{
	inline void require(const bool condition, const std::string_view message)
	{
		if (!condition)
		{
			throw std::runtime_error(std::string{message});
		}
	}

	inline d3d11::device_snapshot create_graphics(const std::uint64_t generation)
	{
		constexpr D3D_FEATURE_LEVEL requested_levels[]{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};

		d3d11::device_snapshot graphics;
		auto create = [&](const D3D_DRIVER_TYPE driver_type)
		{
			return D3D11CreateDevice(nullptr, driver_type, nullptr, 0, requested_levels,
				static_cast<UINT>(std::size(requested_levels)), D3D11_SDK_VERSION,
				&graphics.device, &graphics.feature_level, &graphics.context);
		};

		auto result = create(D3D_DRIVER_TYPE_HARDWARE);
		if (FAILED(result))
		{
			graphics = {};
			result = create(D3D_DRIVER_TYPE_WARP);
		}

		require(SUCCEEDED(result), std::format("D3D11CreateDevice failed (HRESULT=0x{:08x})",
			static_cast<std::uint32_t>(result)));
		graphics.generation = generation;
		return graphics;
	}

	inline d3d11::device_snapshot create_hardware_graphics(const std::uint64_t generation)
	{
		constexpr D3D_FEATURE_LEVEL requested_levels[]{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};

		d3d11::device_snapshot graphics;
		const auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			requested_levels, static_cast<UINT>(std::size(requested_levels)), D3D11_SDK_VERSION,
			&graphics.device, &graphics.feature_level, &graphics.context);
		require(SUCCEEDED(result) && graphics.device != nullptr && graphics.context != nullptr,
			std::format("D3D11CreateDevice(HARDWARE) failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(result)));
		graphics.generation = generation;
		return graphics;
	}

	inline d3d11::device_snapshot create_graphics_on_same_adapter(
		const d3d11::device_snapshot& reference, const std::uint64_t generation)
	{
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		require(reference.device != nullptr, "reference D3D11 device is null");
		require(SUCCEEDED(reference.device.As(&dxgi_device)),
			"reference D3D11 device does not expose IDXGIDevice");
		require(SUCCEEDED(dxgi_device->GetAdapter(&adapter)),
			"reference IDXGIDevice::GetAdapter failed");

		constexpr D3D_FEATURE_LEVEL requested_levels[]{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};
		d3d11::device_snapshot graphics;
		const auto result = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
			nullptr, 0, requested_levels, static_cast<UINT>(std::size(requested_levels)),
			D3D11_SDK_VERSION, &graphics.device, &graphics.feature_level, &graphics.context);
		require(SUCCEEDED(result), std::format(
			"creating a second D3D11 device on the game adapter failed (HRESULT=0x{:08x})",
			static_cast<std::uint32_t>(result)));
		graphics.generation = generation;
		return graphics;
	}

	inline LUID adapter_luid(const d3d11::device_snapshot& graphics)
	{
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		DXGI_ADAPTER_DESC description{};
		require(graphics.device != nullptr, "D3D11 device is null");
		require(SUCCEEDED(graphics.device.As(&dxgi_device)), "ID3D11Device does not expose IDXGIDevice");
		require(SUCCEEDED(dxgi_device->GetAdapter(&adapter)), "IDXGIDevice::GetAdapter failed");
		require(SUCCEEDED(adapter->GetDesc(&description)), "IDXGIAdapter::GetDesc failed");
		return description.AdapterLuid;
	}
}
