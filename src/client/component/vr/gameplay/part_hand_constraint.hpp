#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "hand_pose_library.hpp"

namespace vr::gameplay::weapons
{
	// Presentation only. Never publish the constrained wrist back as controller
	// input: physical travel/breakaway must use the original tracked target.
	template<class Pose>
	inline bool constrain_part_hand(const hands::rig& r, const hands::pose_library& library,
		const Pose& grip, const std::array<hands::anchor,2>& raw_targets,
		const std::array<hands::vec,2>& shoulders, const std::array<hands::vec,3>& body_axis,
		int rear, hands::anchor desired_wrist, std::span<hands::bone> solved,float grip_amount=0.f) noexcept
	{
		using namespace hands;
		if (!std::isfinite(grip_amount) || grip_amount<0 || grip_amount>1 || rear < 0 || rear > 1 || solved.size() < static_cast<size_t>(r.count)) return false;
		const int off=1-rear;
		auto targets=raw_targets;
		// Use the same basis as apply_poses, including mirrored support grips.
		const auto basis=blend_quat(hands::free_hand_rotation(grip,off),grip.wrists[off].rotation,grip_amount);
		targets[off]={desired_wrist.position,normalize(multiply(desired_wrist.rotation,conjugate(basis)))};
		std::array<bone,256> result{}; std::array<bool,2> limited{};
		std::array<float,2> amounts{}; amounts[rear]=1;amounts[off]=grip_amount;
		if (!solve(r,solved,targets,shoulders,body_axis,rear,result,limited,&grip.wrists[rear].position) ||
			!apply_poses(r,library,grip,targets,amounts,false,result)) return false;
		// Preserve IK shoulder/elbow, but let the skinned forearm stretch to the
		// exact constrained wrist rather than letting the hand slip off the part.
		const auto wrist=r.arms[off].wrist;
		const auto delta=sub(desired_wrist.position,result[wrist].position);
		for (int i=0;i<r.count;++i)
			if (!r.weapon_bones[i] && descendant(i,r.arms[off].shoulder,r))
			{
				if (descendant(i,wrist,r)) result[i].position=add(result[i].position,delta);
				solved[i]=result[i];
			}
		return true;
	}
}
