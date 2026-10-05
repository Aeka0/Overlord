#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace vr::spatial_panel
{
	using vec3 = std::array<float, 3>;
	using vec4 = std::array<float, 4>;
	using matrix = std::array<float, 16>;
	using quad = std::array<vec3, 4>; // top left, top right, bottom left, bottom right
	using projected_quad = std::array<vec4, 4>;
	inline constexpr unsigned blur_region_capacity=8;
	struct blur_region
	{
		vec4 bounds{}; // Left/top/right/bottom in normalized source-canvas coordinates.
		float alpha{};
	};
	inline bool finite(const vec3& v) noexcept
	{
		return std::all_of(v.begin(), v.end(), [](float x) {
			return std::isfinite(x) && std::abs(x) < 1e7f;
		});
	}
	inline float dot(const vec3& a, const vec3& b) noexcept
	{
		return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
	}
	inline bool billboard(const vec3& center, const vec3& right, const vec3& up,
		float width, float height, quad& output) noexcept
	{
		output = {};
		if (!finite(center) || !finite(right) || !finite(up) || !std::isfinite(width) ||
			!std::isfinite(height) || width <= 0 || height <= 0 || width > 10000 || height > 10000 ||
			std::abs(dot(right, right)-1) > .002f || std::abs(dot(up, up)-1) > .002f ||
			std::abs(dot(right, up)) > .002f) return false;
		for (int i = 0; i < 4; ++i)
			for (int c = 0; c < 3; ++c)
				output[i][c] = center[c] + right[c] * ((i&1) ? width*.5f : -width*.5f) +
					up[c] * (i < 2 ? height*.5f : -height*.5f);
		return true;
	}
	// Native H2 finalized VP contains rotation/projection, NOT camera translation.
	// Subtract the SAME eye origin once before row-vector multiplication. Caller
	// freezes world corners once for both eyes; never billboard toward each eye.
	enum class clip_mode {whole_quad,hardware};
	inline bool project(const quad& world, const vec3& eye_origin, const matrix& vp,
		float minimum_w, projected_quad& output, bool preserve_depth = false,clip_mode clipping=clip_mode::whole_quad) noexcept
	{
		output = {};
		if (!finite(eye_origin) || !std::isfinite(minimum_w) || minimum_w <= 0 ||
			!std::all_of(vp.begin(), vp.end(), [](float x) { return std::isfinite(x); })) return false;
		projected_quad result{};
		for (int v = 0; v < 4; ++v)
		{
			if (!finite(world[v])) return false;
			for (int c = 0; c < 4; ++c)
			{
				result[v][c] = vp[12+c];
				for (int axis = 0; axis < 3; ++axis)
					result[v][c] += (world[v][axis]-eye_origin[axis]) * vp[axis*4+c];
				if (!std::isfinite(result[v][c])) return false;
			}
			if (clipping==clip_mode::whole_quad && result[v][3] < minimum_w) return false;
			// HUD has a spatial disparity but no scene-depth test. Preserve XY/W;
			// do not inherit native reverse-Z near clipping or temporal depth hacks.
			if (!preserve_depth) result[v][2] = result[v][3] * .5f;
			else if (clipping==clip_mode::whole_quad && (result[v][2]<0 || result[v][2]>result[v][3])) return false;
		}
		// Spatial menu edges can cross the near plane while its center is still
		// visible. Preserve homogeneous coordinates for the GPU to clip instead
		// of dropping the complete blur rectangle on a large head turn.
		if(clipping==clip_mode::hardware && std::none_of(result.begin(),result.end(),[&](const auto& p){return p[3]>=minimum_w;}))return false;
		output = result;
		return true;
	}
}
