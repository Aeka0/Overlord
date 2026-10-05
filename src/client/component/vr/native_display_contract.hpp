#pragma once

#include <d3d11.h>
#include <cstdint>

namespace vr::native_display_contract
{
	inline constexpr std::uint32_t target_count = 2;
	[[nodiscard]] constexpr std::uint32_t target_index(const std::uint32_t id) noexcept
	{
		return id == 4 ? 0 : id == 5 ? 1 : target_count;
	}

	struct route
	{
		std::uint32_t source{};
		std::uint32_t destination{};
		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return (source == 4 && destination == 5) || (source == 5 && destination == 4);
		}
	};

	// The native scene owner canonicalizes the selected ping-pong target at
	// 0x1407A829E. Thermal processing can select 5; the spare then becomes 4.
	// Reject unfinished records and the native unavailable-target sentinel.
	[[nodiscard]] constexpr route resolve(const std::uint32_t final_target,
		const std::uint32_t first, const std::uint32_t spare,
		const std::uint32_t selector) noexcept
	{
		const route result{final_target, spare};
		return final_target == first && selector == 0 && result ? result : route{};
	}

	// Actual ping-pong GetDesc: RTV|SRV|UAV, even though its registry UAV view is
	// null. Descriptor equality cannot distinguish raw HDR from final pixels:
	// the owner must also prove target identity and execute the PostFX pass.
	// PostFX's plain-UNORM desktop RTV receives display-encoded RGB. Native
	// copy converts this encoding to linear, independently of storage format.
	[[nodiscard]] inline bool accepts(const D3D11_TEXTURE2D_DESC& value) noexcept
	{
		return value.Width != 0 && value.Height != 0 && value.MipLevels == 1 &&
			value.ArraySize == 1 && value.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
			value.SampleDesc.Count == 1 && value.SampleDesc.Quality == 0 &&
			value.Usage == D3D11_USAGE_DEFAULT &&
			value.BindFlags == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
				D3D11_BIND_UNORDERED_ACCESS) &&
			value.CPUAccessFlags == 0 && value.MiscFlags == 0;
	}
}
