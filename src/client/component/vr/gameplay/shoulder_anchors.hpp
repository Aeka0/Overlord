#pragma once

#include "hand_pose_solver.hpp"
#include "../head_pose_bridge.hpp"

namespace vr::gameplay::hands
{
	struct shoulder_offsets
	{
		float half_width_meters{0.18f};
		float down_meters{0.20f};
		float back_meters{0.08f};
	};

	// HMD-only body estimate, not tracked shoulders. Gravity stays world-up:
	// head pitch/roll must not place one shoulder over the head or in front of it.
	inline bool make_shoulders(const head_pose_bridge::spatial_frame& frame, const vec& view_offset,
		const shoulder_offsets& offsets, std::array<vec, 2>& output) noexcept
	{
		if (!std::isfinite(offsets.half_width_meters) || offsets.half_width_meters < 0.10f ||
			offsets.half_width_meters > 0.35f || !std::isfinite(offsets.down_meters) ||
			offsets.down_meters < 0.05f || offsets.down_meters > 0.45f ||
			!std::isfinite(offsets.back_meters) || offsets.back_meters < -0.10f ||
			offsets.back_meters > 0.30f || !std::isfinite(frame.units_per_meter) ||
			frame.units_per_meter <= 0 || frame.units_per_meter > 10000) return false;
		for (const auto point : {frame.head_position, view_offset})
			for (const auto value : point)
				if (!std::isfinite(value)) return false;
		for (const auto& row : frame.head_yaw_axis)
			for (const auto value : row)
				if (!std::isfinite(value)) return false;
		const auto& forward = frame.head_yaw_axis[0];
		const auto& left = frame.head_yaw_axis[1];
		if (std::abs(dot(forward, forward) - 1) > 0.001f || std::abs(forward[2]) > 0.001f ||
			length(sub(left, vec{-forward[1], forward[0], 0})) > 0.001f ||
			length(sub(frame.head_yaw_axis[2], vec{0, 0, 1})) > 0.001f) return false;
		const auto center = sub(sub(frame.head_position, view_offset),
			scale(add(scale(forward, offsets.back_meters), vec{0, 0, offsets.down_meters}), frame.units_per_meter));
		const auto side = scale(left, offsets.half_width_meters * frame.units_per_meter);
		const std::array<vec, 2> result{add(center, side), sub(center, side)};
		for (const auto& point : result)
			for (const auto value : point)
				if (!std::isfinite(value)) return false;
		output = result;
		return true;
	}
}
