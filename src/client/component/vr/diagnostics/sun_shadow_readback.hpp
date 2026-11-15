#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace vr::diagnostics::sun_shadow
{
	inline constexpr std::uint64_t memory_limit = 768ull * 1024 * 1024;
	inline constexpr std::size_t readback_budget = 8 * 1024 * 1024;

	// Preserve the resource's format, including typeless depth storage. No
	// resolve, conversion or thumbnail can establish byte-for-byte equivalence.
	inline unsigned pixel_bytes(const DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
		case DXGI_FORMAT_A8_UNORM: case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM: return 1;
		case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM: return 2;
		case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R11G11B10_FLOAT: return 4;
		case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R32G32_FLOAT: return 8;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
		default: return 0;
		}
	}

	inline std::uint64_t surface_bytes(const D3D11_TEXTURE2D_DESC& value) noexcept
	{
		const auto stride = pixel_bytes(value.Format);
		if (!stride || !value.Width || !value.Height || value.MipLevels != 1 ||
			value.ArraySize != 1 || value.SampleDesc.Count != 1 || value.SampleDesc.Quality)
			return 0;
		const auto bytes = std::uint64_t(value.Width) * value.Height * stride;
		return bytes <= memory_limit ? bytes : 0;
	}

	inline HRESULT create_staging(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& source,
		ID3D11Texture2D** output) noexcept
	{
		if (!device || !output || !surface_bytes(source) ||
			device->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_1) return E_INVALIDARG;
		auto description = source;
		description.Usage = D3D11_USAGE_STAGING;
		description.BindFlags = description.MiscFlags = 0;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		return device->CreateTexture2D(&description, nullptr, output);
	}

	// One bounded chunk, with no RowPitch padding in the CPU evidence. Repeated
	// nonblocking Maps are safe because this private staging surface is immutable.
	inline HRESULT read_rows(ID3D11DeviceContext* context, ID3D11Texture2D* staging,
		const D3D11_TEXTURE2D_DESC& description, std::uint8_t* output,
		unsigned& row, std::size_t& budget) noexcept
	{
		const auto row_bytes = std::size_t(description.Width) * pixel_bytes(description.Format);
		if (!context || !staging || !output || !row_bytes || row > description.Height ||
			context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return E_INVALIDARG;
		if (row == description.Height || budget < row_bytes) return S_FALSE;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const auto result = context->Map(staging, 0, D3D11_MAP_READ,
			D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
		if (FAILED(result)) return result;
		if (!mapped.pData || mapped.RowPitch < row_bytes)
		{
			context->Unmap(staging, 0);
			return E_FAIL;
		}
		const auto count = (std::min)(std::size_t(description.Height - row), budget / row_bytes);
		for (std::size_t i{}; i < count; ++i)
			std::memcpy(output + (row + i) * row_bytes,
				static_cast<const std::uint8_t*>(mapped.pData) + (row + i) * mapped.RowPitch, row_bytes);
		context->Unmap(staging, 0);
		row += static_cast<unsigned>(count);
		budget -= count * row_bytes;
		return S_OK;
	}

	struct pixel_comparison
	{
		std::uint64_t differences{}, first{};
		bool left_varied{}, right_varied{};
	};
	inline pixel_comparison compare_pixels(const std::uint8_t* left, const std::uint8_t* right,
		const std::size_t size, const unsigned stride) noexcept
	{
		pixel_comparison result{}; result.first = size;
		if (size > stride && stride)
		{
			result.left_varied = std::memcmp(left, left + stride, size - stride) != 0;
			result.right_varied = std::memcmp(right, right + stride, size - stride) != 0;
		}
		if (std::memcmp(left, right, size) == 0) return result;
		std::size_t first{};
		while (first < size && left[first] == right[first]) ++first;
		result.first = first;
		for (auto i = first; i < size; ++i) result.differences += left[i] != right[i];
		return result;
	}
}
