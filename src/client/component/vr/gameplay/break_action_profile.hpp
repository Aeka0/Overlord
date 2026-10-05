#pragma once
#include "break_action_gesture.hpp"
#include "part_grip_pose.hpp"
#include "weapon_sound_reference.hpp"

namespace vr::gameplay::weapons
{
	struct hinged_part_pose{std::string_view name;hands::anchor closed,open;};
	struct break_action_profile
	{
		std::string_view id,native_name,receiver;unsigned receiver_bones{};
		break_action::rules ammunition;break_action::tuning interaction;
		std::string_view barrel_bone,lock_bone,shell_bone,case_bone;
		std::array<std::string_view,2> chamber_bones;
		hands::anchor barrel_closed,barrel_open,lock_closed,lock_open,shell_in_wrist;
		std::array<hands::anchor,2> chamber_in_barrel;
		std::array<hands::anchor,2> mouth_in_barrel;
		hands::anchor case_in_shell{};
		hands::vec hinge_pivot{}; // Fixed pivot reconstructed from the native closed/open rigid transforms.
		hands::vec shell_tip{}; // Round-local front endpoint, used for physical insertion.
		part_grip_pose barrel_grip; // Closed-barrel gun-local grip; rotates with the hinge.
		std::span<const joint_pose> shell_fingers;
		sound_reference (*sound_key)(break_action::effect){};
		std::span<const hinged_part_pose> followers{};
		bool scatter_ejected_rounds{}; // Presentation only, after clearing the breech.
		bool matches_native(std::string_view name,int capacity)const noexcept
		{return name==native_name && capacity==int(ammunition.capacity);}
	};
	inline hands::anchor hinge_pose(hands::anchor closed,hands::anchor open,float amount)noexcept
	{
		amount=std::clamp(amount,0.f,1.f);
		return {hands::add(closed.position,hands::scale(hands::sub(open.position,closed.position),amount)),hands::blend_quat(closed.rotation,open.rotation,amount)};
	}
	inline hands::anchor barrel_pose(const break_action_profile& p,float amount)noexcept
	{
		const float half=std::clamp(amount,0.f,1.f)*p.interaction.open_angle*.5f;
		const hands::quat rotation{0,std::sin(half),0,std::cos(half)};
		return {hands::add(p.hinge_pivot,hands::rotate(rotation,hands::sub(p.barrel_closed.position,p.hinge_pivot))),
			hands::normalize(hands::multiply(rotation,p.barrel_closed.rotation))};
	}
}
