#pragma once
#include "hands/pose_library.hpp"
#include "weapon_profile.hpp"
#include "trigger_discipline_profiles.hpp"

namespace vr::gameplay::hands
{
	inline pose_library bind_weapon_poses(const rig& r,std::span<const bone_definition> bones,const weapons::profile& p) noexcept
	{
		auto out=bind_poses(r,bones,{p.id,p.fingers,p.equip_rest});
		out.safe_index=trigger_index::find(p.id);
		return out;
	}
	// Shared rest assembly for held and stored weapons. No hand ownership or
	// interaction publication is needed to assemble a receiver on first use.
	template<class Pose>
	inline bool apply_weapon_rest(const rig& r,const pose_library& library,const Pose& profile,
		std::span<bone> pose) noexcept
	{
		if(!library.valid || r.count<=0 || r.count>256 || pose.size()<static_cast<size_t>(r.count))return false;
		std::array<bone,256> before{};std::copy_n(pose.begin(),r.count,before.begin());
		for(int i=0;i<r.count;++i)
		{
			if(!r.weapon_bones[i] || i==r.gun)continue;
			const auto parent=r.parent[i];if(parent<0 || parent>=i)return false;
			const auto inverse=conjugate(normalize(before[parent].rotation));
			anchor local{rotate(inverse,sub(before[i].position,before[parent].position)),normalize(multiply(inverse,normalize(before[i].rotation)))};
			if(library.part[i]>=0)local=profile.equip_rest[library.part[i]].local;
			pose[i].position=add(pose[parent].position,rotate(pose[parent].rotation,local.position));
			pose[i].rotation=normalize(multiply(pose[parent].rotation,local.rotation));
		}
		return true;
	}
	// Evaluate compact parent-relative finger poses onto THIS model's joint
	// positions. Bind/rest fingers supply a relaxed open pose, not identity quats.
	template<class Pose>
	inline bool apply_poses(const rig& r, const pose_library& library, const Pose& profile,
							const std::array<anchor, 2>& targets, const std::array<float, 2>& grip_amount,
							bool suppress_equip, std::span<bone> pose) noexcept
	{
		if (!library.valid || pose.size() < static_cast<size_t>(r.count))
			return false;
		std::array<bone, 256> before{};
		std::copy_n(pose.begin(), r.count, before.begin());
		for (int h = 0; h < 2; ++h)
		{
			const auto wrist = r.arms[h].wrist;
			const auto basis=blend_quat(hands::free_hand_rotation(profile,h),profile.wrists[h].rotation,grip_amount[h]);
			pose[wrist].rotation = normalize(multiply(targets[h].rotation, basis));
			for (int i = wrist + 1; i < r.count; ++i)
			{
				if (!descendant(i, wrist, r) || r.weapon_bones[i])
					continue;
				const auto parent = r.parent[i];
				const auto inverse = conjugate(normalize(before[parent].rotation));
				anchor local{rotate(inverse, sub(before[i].position, before[parent].position)),
							 normalize(multiply(inverse, normalize(before[i].rotation)))};
				if (library.finger[i] >= 0)
				{
					local = library.rest_local[i];
					local.rotation = blend_quat(local.rotation, profile.fingers[library.finger[i]].rotation,
												grip_amount[h]);
				}
				pose[i].position = add(pose[parent].position, rotate(pose[parent].rotation, local.position));
				pose[i].rotation = normalize(multiply(pose[parent].rotation, local.rotation));
			}
		}
		if(suppress_equip)return apply_weapon_rest(r,library,profile,pose);
		return true;
	}
}
