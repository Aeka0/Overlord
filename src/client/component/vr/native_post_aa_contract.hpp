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
	inline constexpr std::uint32_t smaa_edge_target = 14, smaa_blend_target = 15;
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

	enum class resource_role { ldr_color, smaa_edges, smaa_blend };
	[[nodiscard]] constexpr resource_role target_role(std::uint32_t target) noexcept
	{
		return target == smaa_edge_target ? resource_role::smaa_edges :
			target == smaa_blend_target ? resource_role::smaa_blend : resource_role::ldr_color;
	}
	[[nodiscard]] constexpr const char* role_name(resource_role role) noexcept
	{
		return role == resource_role::smaa_edges ? "smaa_edges" :
			role == resource_role::smaa_blend ? "smaa_blend" : "ldr_color";
	}
	[[nodiscard]] constexpr const char* texture_formats(resource_role role) noexcept
	{
		return role == resource_role::smaa_edges ?
			"RG8_UNORM(49)/RGBA8_TYPELESS(27)/RGBA8_UNORM(28)" : "RGBA8_TYPELESS(27)/RGBA8_UNORM(28)";
	}
	[[nodiscard]] constexpr const char* view_formats(resource_role role) noexcept
	{
		return role == resource_role::smaa_edges ? "RG8_UNORM(49)/RGBA8_UNORM(28)" : "RGBA8_UNORM(28)";
	}
	[[nodiscard]] constexpr bool accepts_texture_format(resource_role role, DXGI_FORMAT format) noexcept
	{
		return format == DXGI_FORMAT_R8G8B8A8_TYPELESS || format == DXGI_FORMAT_R8G8B8A8_UNORM ||
			(role == resource_role::smaa_edges && format == DXGI_FORMAT_R8G8_UNORM);
	}

	// H2's SMAA edge target can be RG8 on AMD and RGBA8 on NVIDIA. Blend
	// weights and LDR/history retain RGBA8; never apply the edge exception to
	// final colors. Views must match the storage family and remain non-sRGB.
	[[nodiscard]] inline const char* resource_rejection(resource_role role, const D3D11_TEXTURE2D_DESC& texture,
		const D3D11_RENDER_TARGET_VIEW_DESC& output, const D3D11_SHADER_RESOURCE_VIEW_DESC& input,
		std::uint32_t width, std::uint32_t height) noexcept
	{
		if (!width || !height) return "expected_extent";
		if (texture.Width != width || texture.Height != height) return "texture_extent";
		if (!accepts_texture_format(role, texture.Format))
			return "texture_format";
		if (texture.ArraySize != 1) return "texture_array";
		if (texture.MipLevels != 1) return "texture_mips";
		if (texture.SampleDesc.Count != 1 || texture.SampleDesc.Quality != 0) return "texture_samples";
		if (texture.Usage != D3D11_USAGE_DEFAULT) return "texture_usage";
		if (texture.CPUAccessFlags != 0) return "texture_cpu_access";
		if (texture.MiscFlags != 0) return "texture_misc";
		constexpr auto bindings = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if ((texture.BindFlags & bindings) != bindings) return "texture_bindings";
		const auto view_format = texture.Format == DXGI_FORMAT_R8G8_UNORM ?
			DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
		if (output.Format != view_format) return "rtv_format";
		if (output.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D) return "rtv_dimension";
		if (output.Texture2D.MipSlice != 0) return "rtv_mip";
		if (input.Format != view_format) return "srv_format";
		if (input.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) return "srv_dimension";
		if (input.Texture2D.MostDetailedMip != 0 || input.Texture2D.MipLevels != 1) return "srv_mips";
		return nullptr;
	}
	[[nodiscard]] inline const char* target_rejection(std::uint32_t target, const D3D11_TEXTURE2D_DESC& texture,
		const D3D11_RENDER_TARGET_VIEW_DESC& output, const D3D11_SHADER_RESOURCE_VIEW_DESC& input,
		std::uint32_t width, std::uint32_t height) noexcept
	{
		return resource_rejection(target_role(target), texture, output, input, width, height);
	}
	[[nodiscard]] inline const char* ldr_rejection(const D3D11_TEXTURE2D_DESC& texture,
		const D3D11_RENDER_TARGET_VIEW_DESC& output, const D3D11_SHADER_RESOURCE_VIEW_DESC& input,
		std::uint32_t width, std::uint32_t height) noexcept
	{
		return resource_rejection(resource_role::ldr_color, texture, output, input, width, height);
	}
	[[nodiscard]] inline bool accepts_ldr(const D3D11_TEXTURE2D_DESC& texture,
		const D3D11_RENDER_TARGET_VIEW_DESC& output, const D3D11_SHADER_RESOURCE_VIEW_DESC& input,
		std::uint32_t width, std::uint32_t height) noexcept
	{
		return ldr_rejection(texture, output, input, width, height) == nullptr;
	}

	struct view_identity
	{
		std::uint64_t pair{};
		std::uint64_t device_generation{};
		std::uint32_t eye{}; // 0/1 output eyes, 2 auxiliary optical view.
		bool reset_history{};
	};
}
