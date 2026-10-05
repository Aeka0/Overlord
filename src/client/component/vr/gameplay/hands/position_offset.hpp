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
	// Defaults: user-tuned Quest 3 values, not measured anatomy or an automatic
	// controller profile. Other controllers may need their own numeric tuning.
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
	// Preserve the physical grip-to-wrist lever separately from alignment.
	// Alignment translates the whole wrist frame in the player's reference
	// space, using the runtime's raw-grip/aim relation for its neutral basis.
	// Only the physical lever follows wrist rotation; the cosmetic delta must
	// not sweep an arc when the user changes Up/Back/Inward. Neither corrected
	// aim nor HMD looking direction defines this translation. Use the raw PAIR
	// for the device relation: filtered grip with raw aim would rotate alignment
	// by the filter's angular lag. The physical lever still uses filtered grip.
	inline bool make_wrist_target(const head_pose_bridge::world_pose& grip,
		const head_pose_bridge::world_pose& aim, const head_pose_bridge::world_pose& runtime_grip,
		const head_pose_bridge::world_pose& runtime_aim,
		const std::array<vec,3>& reference_axis, const vec& view_offset, float units_per_meter,
		int hand, const position_offsets& offsets, const position_offsets& pivot, anchor& output) noexcept
	{
		anchor result;
		vec physical{},raw_physical{},aligned{};
		if (!valid_offset_axis(aim.axis) || !valid_offset_axis(runtime_aim.axis) || !valid_offset_axis(reference_axis) ||
			!offset_wrist({},grip.axis,units_per_meter,hand,pivot,physical) ||
			!offset_wrist({},runtime_grip.axis,units_per_meter,hand,pivot,raw_physical) ||
			!offset_wrist({},runtime_grip.axis,units_per_meter,hand,offsets,aligned)) return false;
		const auto delta=sub(aligned,raw_physical);
		result.position=add(sub(grip.position,view_offset),physical);
		for (unsigned i=0;i<3;++i)
			result.position=add(result.position,scale(reference_axis[i],dot(runtime_aim.axis[i],delta)));
		for (float x:result.position) if (!std::isfinite(x)) return false;
		result.rotation = from_axis(aim.axis);
		output = result;
		return true;
	}
	// Mechanical controls can select raw tracking and apply their own common
	// filter; the default remains the stabilized visual/interaction wrist.
	inline bool tracked_wrist(const controller_input::frame& input,const head_pose_bridge::spatial_frame& body,
		const vec& view_offset,int hand,const position_offsets& offsets,anchor& output,bool raw=false) noexcept
	{
		if (hand<0 || hand>1 || !input.grip[hand].valid || !input.aim[hand].valid ||
			!input.runtime_grip[hand].valid || !input.runtime_aim[hand].valid) return false;
		head_pose_bridge::world_pose grip,aim,raw_grip,raw_aim;
		const auto& p=input.wrist_pivot_meters;
		return head_pose_bridge::tracking_to_world(body,(raw?input.runtime_grip[hand]:input.grip[hand]).tracking,grip) &&
			head_pose_bridge::tracking_to_world(body,(raw?input.runtime_aim[hand]:input.aim[hand]).tracking,aim) &&
			head_pose_bridge::tracking_to_world(body,input.runtime_grip[hand].tracking,raw_grip) &&
			head_pose_bridge::tracking_to_world(body,input.runtime_aim[hand].tracking,raw_aim) &&
			make_wrist_target(grip,aim,raw_grip,raw_aim,body.world_yaw_axis,view_offset,body.units_per_meter,hand,
				offsets,{p[0],p[1],p[2]},output);
	}
}
