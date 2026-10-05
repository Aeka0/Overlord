#pragma once
#include "lever_gesture.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"

namespace vr::gameplay::weapons::lever
{
	struct moving_part {std::string_view name;hands::anchor closed,open;std::string_view parent;};
	struct profile
	{
		hands::anchor wrist_closed,wrist_open;
		std::span<const hands::anchor> spin_wrists;
		std::span<const moving_part> parts;
		std::span<const joint_pose> open_fingers;
		std::span<const std::string_view> hidden_rounds;
		std::span<const joint_pose> closed_fingers;
		std::span<const hands::quat> spin_fingers;
	};
	inline hands::anchor blend(hands::anchor a,hands::anchor b,float t)noexcept
	{return {hands::add(a.position,hands::scale(hands::sub(b.position,a.position),t)),hands::blend_quat(a.rotation,b.rotation,t)};}
	inline hands::quat finger_rotation(const profile& p,size_t joint,pose_state motion)noexcept
	{
		const auto rest=p.closed_fingers[joint].rotation,open=p.open_fingers[joint].rotation;
		if(!motion.grasped)return rest;
		if(!motion.spinning || p.spin_wrists.size()<2 || p.spin_fingers.size()!=p.spin_wrists.size()*p.open_fingers.size())
			return hands::blend_quat(rest,open,motion.open);
		const float frame=std::clamp(motion.spin,0.f,1.f)*float(p.spin_wrists.size()-1);
		const auto i=std::min(size_t(frame),p.spin_wrists.size()-2),stride=p.open_fingers.size();
		auto rotation=hands::blend_quat(p.spin_fingers[i*stride+joint],p.spin_fingers[(i+1)*stride+joint],frame-float(i));
		if(frame<6)rotation=hands::blend_quat(hands::blend_quat(rest,open,motion.entry_open),rotation,frame/6);
		return rotation;
	}

	inline hands::anchor gun_offset(const profile& p,pose_state motion,hand rear)noexcept
	{
		using namespace hands::pose_math;
		if(!motion.grasped)return {{},{0,0,0,1}};
		auto wrist=blend(p.wrist_closed,p.wrist_open,std::clamp(motion.open,0.f,1.f));
		if(motion.spinning && p.spin_wrists.size()>1)
		{
			const float frame=std::clamp(motion.spin,0.f,1.f)*float(p.spin_wrists.size()-1);
			const auto i=std::min(size_t(frame),p.spin_wrists.size()-2);
			wrist=blend(p.spin_wrists[i],p.spin_wrists[i+1],frame-float(i));
			if(frame<6)wrist=blend(blend(p.wrist_closed,p.wrist_open,motion.entry_open),wrist,frame/6);
		}
		auto offset=compose(p.wrist_closed,inverse(wrist));
		// Mirrored wrist bases cancel in closed * inverse(moving). This keeps
		// the native gun and its asymmetric parts intact for either physical hand.
		if(rear==hand::left){offset.position=hands::pose_mirror::position(offset.position);offset.rotation=hands::pose_mirror::rotation(offset.rotation);}
		return offset;
	}
	inline hands::anchor apply_gun_pose(hands::anchor base,hands::anchor offset,hands::anchor control,
		const hands::anchor* support=nullptr)noexcept
	{
		using namespace hands::pose_math;
		auto gun=compose(base,offset);
		if(!support)return gun;
		// Re-solve the two-contact direction with the moving control anchor.
		// Applying the one-hand orbit after two-hand aiming would pull the front
		// hand sideways. Weapon scale and tracked controller positions stay fixed.
		const auto moving=compose(inverse(offset),control);
		const auto rear=compose(base,control).position,front=compose(base,*support).position;
		gun.rotation=aimed_rotation(aim_rule::two_hand,gun.rotation,rear,front,sub(support->position,moving.position));
		gun.position=sub(rear,rotate(gun.rotation,moving.position));return gun;
	}
}
