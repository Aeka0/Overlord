#pragma once
#include <array>
#include <cmath>
#include <cstddef>

namespace vr::native_flare_geometry
{
	using vector = std::array<float, 3>;
	struct camera { vector origin; std::array<vector, 3> axis; float tan_x{}, tan_y{}; };
	struct vertex { vector position; std::array<std::byte, 36> attributes; };
	static_assert(sizeof(vertex) == 48);
	using quad = std::array<vertex, 4>;
	inline double dot(const vector& a, const vector& b) noexcept
	{ return double(a[0])*b[0] + double(a[1])*b[1] + double(a[2])*b[2]; }
	inline bool bounded(double value) noexcept { return std::isfinite(value) && std::abs(value) < 1.e8; }

	// H2 has already computed animation, radial rotation, ghost-position factor,
	// color and UVs. Lift only its NDC xyz onto the source's camera-depth plane.
	// One immutable world quad can then use both eyes without changing uploads.
	inline bool to_world(const camera& c, const vector& light, quad& vertices) noexcept
	{
		if (!std::isfinite(c.tan_x) || !std::isfinite(c.tan_y) ||
			c.tan_x <= .001f || c.tan_y <= .001f || c.tan_x > 100 || c.tan_y > 100) return false;
		for (unsigned i = 0; i < 3; ++i)
		{
			if (!bounded(light[i]) || !bounded(c.origin[i]) || std::abs(dot(c.axis[i], c.axis[i])-1.) > .01) return false;
			for (unsigned j = 0; j < 3; ++j)
				if (!bounded(c.axis[i][j]) || (j != i && std::abs(dot(c.axis[i], c.axis[j])) > .01)) return false;
		}
		const vector cross{c.axis[0][1]*c.axis[1][2]-c.axis[0][2]*c.axis[1][1],
			c.axis[0][2]*c.axis[1][0]-c.axis[0][0]*c.axis[1][2], c.axis[0][0]*c.axis[1][1]-c.axis[0][1]*c.axis[1][0]};
		if (dot(cross, c.axis[2]) < .99) return false;
		double depth{};
		for (unsigned i = 0; i < 3; ++i) depth += (double(light[i])-c.origin[i])*c.axis[0][i];
		if (!bounded(depth) || depth <= .001) return false;
		auto output = vertices;
		for (auto& v : output)
		{
			if (!bounded(v.position[0]) || !bounded(v.position[1]) || v.position[2] != 1.f) return false;
			const double x = v.position[0], y = v.position[1];
			for (unsigned i = 0; i < 3; ++i)
			{
				const auto value = c.origin[i] + depth*(c.axis[0][i] - x*c.tan_x*c.axis[1][i] + y*c.tan_y*c.axis[2][i]);
				if (!bounded(value)) return false;
				v.position[i] = static_cast<float>(value);
			}
		}
		vertices = output;
		return true;
	}

	// Native special-flare mode has zero eye/world offsets and identity WORLD0.
	// Bake the exact eye's absolute origin into its row-vector view and VP; do
	// not subtract it again in a native model-placement/eye-offset constant.
	inline bool absolute_matrices(const std::array<float, 64>& relative,
		const vector& origin, std::array<float, 64>& output) noexcept
	{
		for (auto value : relative) if (!bounded(value)) return false;
		for (auto value : origin) if (!bounded(value)) return false;
		// H2's positive-forward, eye-relative perspective contract. Reject an
		// empty/menu/orthographic record instead of publishing a zero transform.
		if (relative[12] != 0 || relative[13] != 0 || relative[14] != 0 || relative[15] != 1 ||
			relative[16] <= 0 || relative[21] <= 0 || relative[27] != 1 || relative[31] != 0 || relative[30] <= 0)
			return false;
		auto result = relative;
		for (const unsigned offset : {0u, 32u})
			for (unsigned column = 0; column < 4; ++column)
			{
				double value = relative[offset + 12 + column];
				for (unsigned row = 0; row < 3; ++row) value -= double(origin[row])*relative[offset + row*4 + column];
				if (!bounded(value)) return false;
				result[offset + 12 + column] = static_cast<float>(value);
			}
		for (unsigned row = 0; row < 4; ++row)
			for (unsigned column = 0; column < 3; ++column)
			{
				const auto value = double(relative[48 + row*4 + column]) + double(origin[column])*relative[48 + row*4 + 3];
				if (!bounded(value)) return false;
				result[48 + row*4 + column] = static_cast<float>(value);
			}
		output = result;
		return true;
	}
}
