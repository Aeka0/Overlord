#pragma once
#include "pose_solver.hpp"
#include <cstddef>
#include <cstring>

namespace vr::gameplay::hands
{
	// Apply once to a newly evaluated native pose, before IK/presentation. Keep
	// anatomical joint positions and reach; size changes the mesh around each
	// segment and the hand/weapon geometry, not the tracked wrist locations.
	inline void size_model_pose(const rig& r, std::span<bone> pose) noexcept
	{
		if (r.model_scale == 1.f) return;
		std::array<vec, 256> positions{};
		for (int i = 0; i < r.count; ++i) positions[i] = pose[i].position;
		for (int i = 0; i < r.count; ++i)
		{
			int root = r.weapon_bones[i] ? r.gun : -1;
			vec segment{};
			if(root<0)for (const auto& limb : r.arms)
			{
				if(descendant(i,limb.wrist,r)){root=limb.wrist;break;}
				if(descendant(i,limb.elbow,r))
				{root=limb.elbow;segment=unit(sub(positions[limb.wrist],positions[limb.elbow]));break;}
				if(descendant(i,limb.shoulder,r))
				{root=limb.shoulder;segment=unit(sub(positions[limb.elbow],positions[limb.shoulder]));break;}
			}
			if (root >= 0)
			{
				const auto offset=sub(positions[i],positions[root]);
				// Auxiliary sleeve pivots must retain their fraction along the
				// physical limb. Only their radial offset shrinks with its thickness.
				const auto along=scale(segment,dot(offset,segment));
				pose[i].position=add(positions[root],add(along,scale(sub(offset,along),r.model_scale)));
			}
		}
	}
	class model_size_scope
	{
	public:
		model_size_scope(const rig& r,std::span<bone> pose) noexcept : pose_(pose),active_(r.model_scale!=1.f)
		{
			if (!active_) return;
			std::memcpy(before_.data(),pose.data(),pose.size_bytes());
			size_model_pose(r,pose);
		}
		~model_size_scope()
		{
			if (active_) std::memcpy(pose_.data(),before_.data(),pose_.size_bytes());
		}
		model_size_scope(const model_size_scope&)=delete;
		model_size_scope& operator=(const model_size_scope&)=delete;
		void commit() noexcept { active_=false; }
	private:
		std::span<bone> pose_;
		std::array<std::byte,256*sizeof(bone)> before_;
		bool active_{};
	};
}
