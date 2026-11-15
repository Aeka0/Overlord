#include "component/vr/diagnostics/sun_shadow_readback.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace shadow = vr::diagnostics::sun_shadow;
using Microsoft::WRL::ComPtr;

namespace
{
	void require(bool condition, const char* reason)
	{
		if (!condition) throw std::runtime_error(reason);
	}
	void copy_check(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT format, bool depth)
	{
		D3D11_TEXTURE2D_DESC description{};
		description.Width = 37; description.Height = 19;
		description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
		description.Format = format; description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE | (depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET);
		const auto row_bytes = description.Width * shadow::pixel_bytes(format);
		std::vector<std::uint8_t> expected(row_bytes * description.Height);
		for (std::size_t i{}; i < expected.size(); ++i) expected[i] = static_cast<std::uint8_t>(i * 37 + i / row_bytes);
		D3D11_SUBRESOURCE_DATA initial{expected.data(), row_bytes, 0};
		ComPtr<ID3D11Texture2D> source, staging;
		require(SUCCEEDED(device->CreateTexture2D(&description, &initial, &source)), "WARP source creation failed");
		require(SUCCEEDED(shadow::create_staging(device, description, &staging)), "staging contract rejected supported texture");
		context->CopyResource(staging.Get(), source.Get());
		// A headless WARP test has no natural Present. Submission is test-only;
		// production read_rows never flushes, waits or issues completion queries.
		context->Flush();
		std::vector<std::uint8_t> actual(expected.size(), 0xCD);
		unsigned row{}, chunks{}, busy{};
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (row != description.Height && std::chrono::steady_clock::now() < deadline)
		{
			const auto previous = row;
			std::size_t budget = row_bytes * 3 + 1;
			const auto result = shadow::read_rows(context, staging.Get(), description, actual.data(), row, budget);
			if (result == DXGI_ERROR_WAS_STILL_DRAWING) { ++busy; SwitchToThread(); continue; }
			require(result == S_OK, "nonblocking row read failed");
			require(row > previous && row - previous <= 3 && budget == row_bytes * 3 + 1 - (row - previous) * row_bytes,
				"row chunk exceeded budget or skipped rows");
			++chunks;
		}
		require(row == description.Height && chunks == 7, "readback never completed in bounded chunks");
		require(actual == expected, "logical pixels differ (including possible RowPitch padding)");
		const auto equal = shadow::compare_pixels(expected.data(), actual.data(), actual.size(), shadow::pixel_bytes(format));
		require(!equal.differences && equal.first == actual.size() && equal.left_varied && equal.right_varied,
			"exact comparison failed to identify nonuniform equal content");
		actual[row_bytes + 7] ^= 1;
		const auto different = shadow::compare_pixels(expected.data(), actual.data(), actual.size(), shadow::pixel_bytes(format));
		require(different.differences == 1 && different.first == row_bytes + 7,
			"single-byte mismatch was accepted or located incorrectly");
		std::printf("format=%u bytes_per_pixel=%u rows=%u chunks=%u busy_polls=%u exact=yes\n", format, shadow::pixel_bytes(format), row, chunks, busy);
	}
}

int main()
{
	try
	{
		ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
		D3D_FEATURE_LEVEL level{};
		require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, &level, &context)), "WARP device creation failed");
		copy_check(device.Get(), context.Get(), DXGI_FORMAT_R16_TYPELESS, true);
		copy_check(device.Get(), context.Get(), DXGI_FORMAT_R32_TYPELESS, true);
		copy_check(device.Get(), context.Get(), DXGI_FORMAT_R24G8_TYPELESS, true);
		copy_check(device.Get(), context.Get(), DXGI_FORMAT_A8_UNORM, false);
		copy_check(device.Get(), context.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, false);
		copy_check(device.Get(), context.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, false);
		copy_check(device.Get(), context.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, false);
		D3D11_TEXTURE2D_DESC unsupported{};
		unsupported.Width = unsupported.Height = 16384;
		unsupported.MipLevels = unsupported.ArraySize = unsupported.SampleDesc.Count = 1;
		unsupported.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		require(!shadow::surface_bytes(unsupported), "oversized texture admitted past memory budget");
		unsupported.Width = unsupported.Height = 37; unsupported.SampleDesc.Count = 4;
		require(!shadow::surface_bytes(unsupported), "MSAA silently admitted without an exact staging representation");
		unsupported.SampleDesc.Count = 1; unsupported.Format = DXGI_FORMAT_BC1_UNORM;
		require(!shadow::surface_bytes(unsupported), "compressed format silently treated as unpacked rows");
		std::puts("sun-shadow staging and bounded logical-row checks passed; no game or VR runtime used");
		return 0;
	}
	catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
