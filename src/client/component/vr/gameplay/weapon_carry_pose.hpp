#pragma once
#include "weapon_profile.hpp"
#include "weapon_carry.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"
#include <optional>

namespace vr::gameplay::weapons::carry
{
	inline hands::quat control_basis(const profile& source,const hands::anchor& grip,hands::quat neutral,int hand) noexcept
	{
		// Inventory blades follow the same anatomical wrist as the empty VR hand.
		// Rotate the object around its authored contact, not the hand off its pivot.
		return source.melee ? hands::normalize(hands::multiply(neutral,hands::conjugate(hands::normalize(grip.rotation)))) :
			source.control_rotations ? (*source.control_rotations)[hand] : hands::quat{0,0,0,1};
	}
	inline hands::anchor control_grip(const profile& source,int hand,hands::quat mirror_basis) noexcept
	{return source.control_grips ? (*source.control_grips)[hand] : hand==1 ? source.wrists[1] : hands::pose_mirror::wrist(source.wrists[1],mirror_basis);}
	inline hands::anchor support_grip(const profile& source,int hand,hands::quat mirror_basis) noexcept
	{return source.support_grips ? (*source.support_grips)[hand] : hand==0 ? source.wrists[0] : hands::pose_mirror::wrist(source.wrists[0],mirror_basis,
		source.support_mirror_center ? *source.support_mirror_center : source.fixed_support_position ? source.wrists[0].position : hands::vec{});}
	// Stack-owned adapter. Mirror the authored hand posture, preserving source
	// profile/library indexes and the actual gun/attachments/mechanical geometry.
	struct pose_profile
	{
		profile value;
		std::array<joint_pose,64> fingers{};
		std::array<hands::anchor,2> controls{};
		std::array<hands::anchor,2> supports{};
		std::array<hands::quat,2> rotations{},support_rotations{};
		hold solver_owner;
		pose_profile(const profile& source,const hold& owner,const hands::rig& rig,const hands::pose_library& library,std::optional<hands::quat> support_rotation=std::nullopt):value(source),solver_owner(owner)
		{
			for(int h=0;h<2;++h)controls[h]=control_grip(source,h,library.mirror_basis[rig.arms[h].wrist]);
			for(int h=0;h<2;++h)supports[h]=support_grip(source,h,library.mirror_basis[rig.arms[h].wrist]);
			for(int h=0;h<2;++h)rotations[h]=control_basis(source,controls[h],library.neutral_wrists[h],h);
			if(source.melee)value.control_rotations=&rotations;
			if(source.control_grips)
			{
				value.authored_rear=int(valid_hand(owner.rear)?owner.rear:owner.pose_rear);
				value.wrists=controls;
			}
			else if (owner.rear==hand::left || (owner.rear==hand::none && owner.pose_rear==hand::left))
			{
				value.authored_rear=0;
				value.wrists[0]=controls[0];
				value.wrists[1]=supports[1];
				value.free_hand_reference=source.free_hand_reference ? source.free_hand_reference : &source.wrists;
				const auto count=std::min(fingers.size(),source.fingers.size());
				std::copy_n(source.fingers.begin(),count,fingers.begin());
				for (int bone=0;bone<rig.count;++bone)
				{
					const int destination=library.finger[bone],other=library.opposite[bone],parent=rig.parent[bone];
					if (destination<0 || other<0 || parent<0 || library.finger[other]<0 || library.opposite[parent]<0) continue;
					fingers[destination].rotation=hands::pose_mirror::local_rotation(library,bone,parent,
						source.fingers[library.finger[other]].rotation);
				}
				value.fingers={fingers.data(),count};
			}
			if (owner.rear==hand::none && valid_hand(owner.support))
			{
				// The support hand carries at its foregrip. This synthetic rear exists
				// only inside the pose solver; the published owner remains unable to fire.
				solver_owner.rear=owner.support;solver_owner.support=hand::none;
				value.authored_rear=static_cast<int>(owner.support);value.aiming=aim_rule::rear_hand;
				value.wrists[value.authored_rear]=supports[value.authored_rear];
				value.forearm=nullptr;
				if(support_rotation){support_rotations=rotations;support_rotations[value.authored_rear]=*support_rotation;value.control_rotations=&support_rotations;}
			}
		}
		pose_profile(const pose_profile&)=delete;
		pose_profile& operator=(const pose_profile&)=delete;
	};
}
