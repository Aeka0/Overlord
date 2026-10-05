#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Shared rigid geometry. Vectors retain the caller's units. Quaternions use
// xyzw active rotations; from_axis accepts row basis vectors. Hand solving,
// presentation and native adapters share these operations without skeleton or
// weapon dependencies. Preserve the existing zero-length normalization rules.
namespace vr::spatial_math
{
	using vec = std::array<float, 3>;
	using quat = std::array<float, 4>; // xyzw, active rotation
	struct anchor
	{
		vec position{};
		quat rotation{0, 0, 0, 1};
	};
	inline vec add(const vec a, const vec b)
	{
		return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
	}
	inline vec sub(const vec a, const vec b)
	{
		return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
	}
	inline vec scale(const vec a, float s)
	{
		return {a[0] * s, a[1] * s, a[2] * s};
	}
	inline float dot(const vec a, const vec b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}
	inline vec cross(const vec a, const vec b)
	{
		return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
	}
	inline float length(const vec a)
	{
		return std::sqrt(dot(a, a));
	}
	inline vec unit(const vec a)
	{
		const auto n = length(a);
		return n > 1e-6f ? scale(a, 1 / n) : vec{1, 0, 0};
	}
	inline quat conjugate(const quat q)
	{
		return {-q[0], -q[1], -q[2], q[3]};
	}
	inline quat normalize(quat q)
	{
		float n{};
		for (auto x : q)
			n += x * x;
		if (n < 1e-10f)
			return {0, 0, 0, 1};
		for (auto& x : q)
			x /= std::sqrt(n);
		return q;
	}
	inline quat multiply(const quat a, const quat b)
	{
		return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
		        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
		        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
		        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
	}
	inline vec rotate(const quat q, const vec v)
	{
		const vec xyz{q[0], q[1], q[2]};
		const auto t = scale(cross(xyz, v), 2);
		return add(v, add(scale(t, q[3]), cross(xyz, t)));
	}
	inline quat from_to(vec a, vec b)
	{
		a = unit(a);
		b = unit(b);
		const auto cosine = std::clamp(dot(a, b), -1.0f, 1.0f);
		if (cosine < -0.9999f)
		{
			const auto axis = unit(cross(a, std::abs(a[0]) < 0.8f ? vec{1, 0, 0} : vec{0, 1, 0}));
			return {axis[0], axis[1], axis[2], 0};
		}
		const auto axis = cross(a, b);
		return normalize({axis[0], axis[1], axis[2], 1 + cosine});
	}
	inline quat from_axis(const std::array<vec, 3>& axis)
	{
		// Basis vectors are rows; transpose to the active column-vector matrix.
		float m[3][3]{};
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				m[r][c] = axis[c][r];
		quat q{};
		const auto trace = m[0][0] + m[1][1] + m[2][2];
		if (trace > 0)
		{
			const float s = 2 * std::sqrt(trace + 1);
			q = {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, s / 4};
		}
		else
		{
			int i = 0;
			if (m[1][1] > m[i][i])
				i = 1;
			if (m[2][2] > m[i][i])
				i = 2;
			const int j = (i + 1) % 3, k = (i + 2) % 3;
			const float s = 2 * std::sqrt(std::max(0.0f, 1 + m[i][i] - m[j][j] - m[k][k]));
			if (s < 1e-6f)
				return {0, 0, 0, 1};
			q[i] = s / 4;
			q[j] = (m[j][i] + m[i][j]) / s;
			q[k] = (m[k][i] + m[i][k]) / s;
			q[3] = (m[k][j] - m[j][k]) / s;
		}
		return normalize(q);
	}
	inline anchor compose(anchor a, anchor b) noexcept
	{
		return {add(a.position, rotate(a.rotation, b.position)), normalize(multiply(a.rotation, b.rotation))};
	}
	inline anchor inverse(anchor a) noexcept
	{
		const auto q = conjugate(normalize(a.rotation));
		return {rotate(q, scale(a.position, -1)), q};
	}
	// rigid_part retains source-model vertices. A desired bone pose therefore
	// needs the inverse source bind exactly once before native rigid submission.
	inline anchor rigid_delta(anchor desired, anchor source_bind) noexcept
	{
		return compose(desired, inverse(source_bind));
	}
}
