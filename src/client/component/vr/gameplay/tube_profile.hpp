#pragma once
#include "tube_gesture.hpp"
#include "weapon_sound_reference.hpp"
#include "lever_pose.hpp"

namespace vr::gameplay::weapons
{
	struct tube_profile
	{
		std::string_view id,receiver;std::span<const std::string_view> native_variants;
		tube::rules ammunition;tube::tuning interaction;
		hands::anchor bolt_rest,lifter_rest,lifter_loaded,shell_in_wrist;
		hands::vec shell_center,port_center,tube_center,port_forward,tube_forward,rack_low,rack_high;
		std::span<const part_grip_pose> rack_grips;std::span<const joint_pose> shell_fingers;
		sound_reference (*sound_key)(tube::effect){};
		std::string_view bolt_bone{"j_bolt"},lifter_bone{"j_load"},shell_bone{"tag_clip"};
		int receiver_bones{12};
		std::string_view pump_bone{};
		hands::anchor pump_rest{},bolt_open{},port_shell{};
		// Fixed-drum single-shell feeds share acquisition/transfer, not tube or
		// bolt semantics. The cylinder remains permanently attached to the receiver.
		std::string_view drum_bone{};
		hands::anchor drum_rest{};
		hands::vec drum_axis{1,0,0};
		const lever::profile* lever{};
		std::optional<hands::anchor> ejection_shell;
		float bolt_return_seconds{}; // Presentation only; uses the shared spring-return transition.
		bool matches_native(std::string_view name,int capacity)const noexcept
		{return capacity==ammunition.capacity && std::find(native_variants.begin(),native_variants.end(),name)!=native_variants.end();}
	};
	inline hands::anchor tube_ejection_pose(const tube_profile& p)noexcept
	{return p.ejection_shell.value_or(hands::anchor{hands::sub(p.port_center,p.shell_center),{0,0,0,1}});}
}
