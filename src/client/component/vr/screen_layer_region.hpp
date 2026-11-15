// Adapted from MWR-VR 442a8edcf5e54c2aeb81a556ff8eab8a84ca3c15.
#pragma once
#include "spatial_panel.hpp"
#include <d3d11.h>

namespace vr::spatial_panel
{
	struct screen_region
	{
		D3D11_RECT draw{};
		D3D11_BOX copy{};
		bool empty() const noexcept { return draw.left >= draw.right || draw.top >= draw.bottom; }
	};
	inline screen_region screen_layer_region(const vec4& canvas, const vec4& fade,
		unsigned width, unsigned height, bool backdrops) noexcept
	{
		screen_region full{{0,0,LONG(width),LONG(height)}, {0,0,0,width,height,1}};
		// screen_ps discards outside the canvas only when its exterior fade is
		// exactly zero. Keep full-field effects and unknown inputs unchanged.
		for (float value : fade) if (value != 0) return full;
		// Extreme projections can lose pixel-sized offsets in shader float
		// arithmetic; don't infer a narrow rectangle from those coordinates.
		for (float value : canvas) if (!std::isfinite(value) || std::abs(value) > 8) return full;
		if (canvas[2] <= 0 || canvas[3] <= 0) return full;
		const auto low = [](double value, unsigned extent) {
			return LONG(std::clamp(std::floor(value * extent) - 1., 0., double(extent)));
		};
		const auto high = [](double value, unsigned extent) {
			return LONG(std::clamp(std::ceil(value * extent) + 1., 0., double(extent)));
		};
		screen_region result;
		result.draw = {low(canvas[0],width), low(canvas[1],height),
			high(double(canvas[0])+canvas[2],width), high(double(canvas[1])+canvas[3],height)};
		// screen_ps uses a radius capped at 8 pixels, plus linear filtering.
		// Copy to identical coordinates; shader UVs and derivatives stay unchanged.
		const LONG halo = backdrops ? 10 : 2;
		result.copy = {UINT(std::max(0L,result.draw.left-halo)), UINT(std::max(0L,result.draw.top-halo)),0,
			UINT(std::min(LONG(width),result.draw.right+halo)), UINT(std::min(LONG(height),result.draw.bottom+halo)),1};
		return result;
	}
}
