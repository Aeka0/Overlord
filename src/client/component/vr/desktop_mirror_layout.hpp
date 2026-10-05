#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "engine_stereo_bridge.hpp"

namespace vr::desktop_mirror
{
	inline constexpr float default_horizontal_fov = 95.0f;
	struct crop
	{
		float u0{}, v0{}, u1{}, v1{}, horizontal_fov{};
		bool limited{};
		[[nodiscard]] explicit operator bool() const noexcept { return u1 > u0 && v1 > v0; }
	};

	// Same eye origin and orientation: a symmetric desktop projection is an
	// affine UV crop. Optical tangents, NOT texture aspect, define its shape.
	// D3D UVs start at top left; tan_up is positive in H2's +Y-up view space.
	// Keep bilinear footprints within the image (half a source texel per edge).
	[[nodiscard]] inline crop project(const engine_stereo_bridge::eye_projection& projection,
		const std::uint32_t source_width, const std::uint32_t source_height,
		const float display_width, const float display_height,
		const float horizontal_fov = default_horizontal_fov) noexcept
	{
		const double left = projection.tan_left, right = projection.tan_right;
		const double down = projection.tan_down, up = projection.tan_up;
		if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(down) || !std::isfinite(up) ||
			left >= 0 || right <= 0 || down >= 0 || up <= 0 || source_width < 2 || source_height < 2 ||
			!std::isfinite(display_width) || !std::isfinite(display_height) || display_width <= 0 || display_height <= 0 ||
			!std::isfinite(horizontal_fov) || horizontal_fov <= 0 || horizontal_fov >= 180)
			return {};
		const auto width = right - left, height = up - down;
		const auto inset_u = 0.5 / source_width, inset_v = 0.5 / source_height;
		const auto available_x = (std::min)(-left, right) - width * inset_u;
		const auto available_y = (std::min)(-down, up) - height * inset_v;
		if (available_x <= 0 || available_y <= 0) return {};
		const auto aspect = static_cast<double>(display_width) / display_height;
		const auto requested_x = std::tan(horizontal_fov * std::numbers::pi / 360.0);
		const auto half_x = (std::min)({requested_x, available_x, available_y * aspect});
		const auto half_y = half_x / aspect;
		return {
			static_cast<float>(std::clamp((-half_x - left) / width, inset_u, 1.0 - inset_u)),
			static_cast<float>(std::clamp((up - half_y) / height, inset_v, 1.0 - inset_v)),
			static_cast<float>(std::clamp((half_x - left) / width, inset_u, 1.0 - inset_u)),
			static_cast<float>(std::clamp((up + half_y) / height, inset_v, 1.0 - inset_v)),
			static_cast<float>(2.0 * std::atan(half_x) * 180.0 / std::numbers::pi), half_x < requested_x};
	}

	// The submit texture is linear (OpenVR ColorSpace_Linear). ImGui's normal
	// pixel shader is for display-encoded UI textures, so the desktop mirror
	// alone must encode to sRGB for H2's SDR UNORM backbuffer.
	inline constexpr char pixel_shader[] = R"(
Texture2D<float4> eye_texture : register(t0);
SamplerState eye_sampler : register(s0);
#ifdef STABILIZED
cbuffer Stabilization : register(b0) { float4 uv_row0; float4 uv_row1; float4 uv_row2; };
#endif
struct input_vertex { float4 position : SV_POSITION; float4 color : COLOR0; float2 uv : TEXCOORD0; };
float4 main(input_vertex input) : SV_Target
{
    float2 uv = input.uv;
#ifdef STABILIZED
    float3 h = float3(dot(uv_row0.xyz,float3(uv,1)),dot(uv_row1.xyz,float3(uv,1)),dot(uv_row2.xyz,float3(uv,1)));
    uv = h.xy / h.z;
#endif
    float3 linear_rgb = saturate(eye_texture.Sample(eye_sampler, uv).rgb);
    float3 high = 1.055 * pow(linear_rgb, 1.0 / 2.4) - 0.055;
    float3 srgb = float3(linear_rgb.r <= 0.0031308 ? 12.92 * linear_rgb.r : high.r,
                        linear_rgb.g <= 0.0031308 ? 12.92 * linear_rgb.g : high.g,
                        linear_rgb.b <= 0.0031308 ? 12.92 * linear_rgb.b : high.b);
    return float4(srgb, 1.0);
}
)";
}
