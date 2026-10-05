#pragma once

#include <array>
#include <cmath>

namespace vr::game_view
{
	// Continuous Euler angles belong to native input, not horizontal body facing.
	// Rendering continues to use the complete tracked rotation. Pick the nearest
	// equivalent Euler branch so crossing either pitch pole does not turn yaw.
	struct continuous_angles
	{
		float pitch{}, yaw{}, roll{}; // H2 axes, with positive pitch looking up.

		bool update(const std::array<std::array<float, 3>, 3>& axis) noexcept
		{
			for (const auto& row : axis)
				for (const auto value : row) if (!std::isfinite(value)) return false;
			constexpr float degrees = 57.29577951308232f;
			const auto horizontal = std::hypot(axis[0][0], axis[0][1]);
			continuous_angles next{std::atan2(axis[0][2], horizontal) * degrees, yaw, roll};
			if (horizontal < 0.0001f)
			{
				// At the singularity retain heading and recover the remaining twist
				// from the left axis, including a rolled or upside-down headset.
				const auto c = std::cos(yaw / degrees), s = std::sin(yaw / degrees);
				const auto sign = axis[0][2] < 0 ? -1.0f : 1.0f;
				next.roll = std::atan2(-sign * (axis[1][0] * c + axis[1][1] * s),
					-axis[1][0] * s + axis[1][1] * c) * degrees;
			}
			else
			{
				next.yaw = std::atan2(axis[0][1], axis[0][0]) * degrees;
				next.roll = std::atan2(axis[1][2], axis[2][2]) * degrees;
			}
			const continuous_angles alternate{180.0f - next.pitch, next.yaw + 180.0f, next.roll + 180.0f};
			const auto distance = [this](const continuous_angles& value) {
				const auto p = std::remainder(value.pitch - pitch, 360.0f);
				const auto y = std::remainder(value.yaw - yaw, 360.0f);
				const auto r = std::remainder(value.roll - roll, 360.0f);
				return p * p + y * y + r * r;
			};
			if (distance(alternate) < distance(next)) next = alternate;
			pitch = std::remainder(next.pitch, 360.0f);
			yaw = std::remainder(next.yaw, 360.0f);
			roll = std::remainder(next.roll, 360.0f);
			return true;
		}
	};

	// Yaw of the tilt-free rotation (fused yaw), independent of Euler branch.
	// H2 stores forward/left/up as rows. This is atan2(2*w*z, w*w-z*z)
	// expressed directly through the rotation matrix. Pure pitch, even past a
	// vertical pole, cannot reverse the shoulders. Only a fully inverted up axis
	// is singular; keep the previous heading within eight degrees of inversion.
	struct horizontal_heading
	{
		float yaw{};
		bool update(const std::array<std::array<float, 3>, 3>& axis) noexcept
		{
			for (const auto& row : axis)
				for (float value : row) if (!std::isfinite(value)) return false;
			const float sine = axis[0][1] - axis[1][0];
			const float cosine = axis[0][0] + axis[1][1];
			if (std::hypot(sine, cosine) >= .01f)
				yaw = std::atan2(sine, cosine) * 57.29577951308232f;
			return true;
		}
	};
}
