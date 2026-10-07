#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>

namespace vr::native_post_aa
{
	// Native r_postAA / scene-record +0x20A4. Value 6 is H2's internal
	// alternate FXAA path, not another setting exposed by the video menu.
	enum class mode : std::uint16_t
	{
		none, fxaa, smaa, smaa_t2x, filmic_smaa, filmic_smaa_t2x, fxaa_internal,
	};

	inline constexpr std::size_t mode_offset = 0x20A4;
	inline constexpr std::uint32_t display_input_target = 13;
	inline constexpr std::uint32_t filmic_scratch_target = 12;
	inline constexpr std::array<std::uint32_t, 6> history_targets{16, 17, 18, 19, 20, 21};

	[[nodiscard]] constexpr bool supported(const mode value) noexcept
	{
		return value <= mode::fxaa_internal;
	}
	[[nodiscard]] constexpr bool temporal(const mode value) noexcept
	{
		return value == mode::smaa_t2x || value == mode::filmic_smaa_t2x;
	}
	[[nodiscard]] constexpr bool filmic(const mode value) noexcept
	{
		return value == mode::filmic_smaa || value == mode::filmic_smaa_t2x;
	}
	[[nodiscard]] constexpr bool needs_history(const mode value, const std::uint32_t target) noexcept
	{
		return (target >= 16 && target <= 19 && temporal(value)) ||
			(target >= 20 && target <= 21 && filmic(value));
	}

	// H2's LDR ping-pong images use TYPELESS storage with plain-UNORM views;
	// SMAA scratch/history images use typed UNORM storage. Both preserve the
	// same encoded RGBA values. An sRGB view would change that color contract.
	[[nodiscard]] inline bool accepts_ldr(const D3D11_TEXTURE2D_DESC& texture,
		const D3D11_RENDER_TARGET_VIEW_DESC& output, const D3D11_SHADER_RESOURCE_VIEW_DESC& input,
		std::uint32_t width, std::uint32_t height) noexcept
	{
		return width != 0 && height != 0 && texture.Width == width && texture.Height == height &&
			(texture.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS || texture.Format == DXGI_FORMAT_R8G8B8A8_UNORM) &&
			texture.ArraySize == 1 && texture.MipLevels == 1 && texture.SampleDesc.Count == 1 &&
			texture.SampleDesc.Quality == 0 && texture.Usage == D3D11_USAGE_DEFAULT &&
			texture.CPUAccessFlags == 0 && texture.MiscFlags == 0 &&
			(texture.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) ==
			(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE) &&
			output.Format == DXGI_FORMAT_R8G8B8A8_UNORM && output.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D &&
			output.Texture2D.MipSlice == 0 && input.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
			input.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && input.Texture2D.MostDetailedMip == 0 &&
			input.Texture2D.MipLevels == 1;
	}

	struct view_identity
	{
		std::uint64_t pair{};
		std::uint64_t device_generation{};
		std::uint32_t eye{}; // 0/1 output eyes, 2 auxiliary optical view.
		bool reset_history{};
	};
}
