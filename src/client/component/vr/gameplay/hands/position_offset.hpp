#pragma once
#include "pose_solver.hpp"
#include "../../head_pose_bridge.hpp"
#include "../../controller_input.hpp"
#include "../../settings.hpp"

namespace vr::gameplay::hands
{
	// Rigid grip-local point-to-wrist translation. During rotation about a real
	// wrist, the tracked controller origin MOVES; R_grip * offset cancels that
	// motion when the offset is correct. A fixed-world translation cannot do so.
	// Defaults and presets are tuning starting points, not measured anatomy or
	// runtime controller metadata. Other controllers may need different values.
	inline constexpr float max_position_offset_meters = settings::max_hand_offset;
	struct position_offsets
	{
		float inward_meters{settings::hand_inward.default_value}, back_meters{settings::hand_back.default_value},
			up_meters{settings::hand_up.default_value};
	};
	inline bool valid_offset_axis(const std::array<vec, 3>& axis) noexcept
	{
		for (const auto& row : axis)
		{
			for (float value : row) if (!std::isfinite(value)) return false;
			if (std::abs(dot(row, row) - 1) > .001f) return false;
		}
		return std::abs(dot(axis[0], axis[1])) <= .001f &&
			length(sub(cross(axis[0], axis[1]), axis[2])) <= .001f;
	}
	inline bool offset_wrist(const vec& controller, const std::array<vec, 3>& grip_axis,
		float units_per_meter, int hand, const position_offsets& offsets, vec& output) noexcept
	{
		if (hand < 0 || hand > 1 || !std::isfinite(units_per_meter) ||
			units_per_meter <= 0 || units_per_meter > 10000) return false;
		for (float value : {offsets.inward_meters, offsets.back_meters, offsets.up_meters})
			if (!std::isfinite(value) || std::abs(value) > max_position_offset_meters) return false;
		for (float value : controller) if (!std::isfinite(value)) return false;
		if (!valid_offset_axis(grip_axis)) return false;
		const auto delta = add(scale(grip_axis[0], -offsets.back_meters),
			add(scale(grip_axis[1], (hand == 0 ? -1.f : 1.f) * offsets.inward_meters),
				scale(grip_axis[2], offsets.up_meters)));
		const auto result = add(controller, scale(delta, units_per_meter));
		for (float value : result) if (!std::isfinite(value)) return false;
		output = result;
		return true;
	}
	// Position calibration selects one rigid point in the controller's grip
	// frame. Head looking and aim-angle calibration cannot translate this point.
	// A stationary anatomical wrist remains stationary when its calibrated lever
	// matches the controller's physical motion; arbitrary cosmetic translations
	// cannot also promise an unchanged physical rotation centre.
	inline bool make_wrist_target(const head_pose_bridge::world_pose& grip,
		const head_pose_bridge::world_pose& aim, const vec& view_offset, float units_per_meter,
		int hand, const position_offsets& offsets, anchor& output) noexcept
	{
		anchor result;
		if (!valid_offset_axis(aim.axis) ||
			!offset_wrist(sub(grip.position,view_offset),grip.axis,units_per_meter,hand,offsets,result.position)) return false;
		result.rotation = from_axis(aim.axis);
		output = result;
		return true;
	}
	// Mechanical controls can select raw tracking and apply their own common
	// filter; the default remains the stabilized visual/interaction wrist.
	inline bool tracked_wrist(const controller_input::frame& input,const head_pose_bridge::spatial_frame& body,
		const vec& view_offset,int hand,const position_offsets& offsets,anchor& output,bool raw=false) noexcept
	{
		if (hand<0 || hand>1) return false;
		const auto& selected_grip=raw?input.runtime_grip[hand]:input.grip[hand];
		const auto& selected_aim=raw?input.runtime_aim[hand]:input.aim[hand];
		if (!selected_grip.valid || !selected_aim.valid) return false;
		head_pose_bridge::world_pose grip,aim;
		return head_pose_bridge::tracking_to_world(body,selected_grip.tracking,grip) &&
			head_pose_bridge::tracking_to_world(body,selected_aim.tracking,aim) &&
			make_wrist_target(grip,aim,view_offset,body.units_per_meter,hand,offsets,output);
	}
}
