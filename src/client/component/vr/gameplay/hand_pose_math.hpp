#pragma once
#include "hand_pose_library.hpp"

namespace vr::gameplay::hands::pose_math
{
	using namespace hands;
	using spatial_math::compose;
	using spatial_math::inverse;
	using spatial_math::rigid_delta;
	inline anchor as_anchor(const bone& b) noexcept
	{
		return {b.position, normalize(b.rotation)};
	}
	inline void move_part(const rig& r, int root, anchor target, std::span<bone> pose) noexcept
	{
		const auto old = pose[root];
		const auto delta = normalize(multiply(target.rotation, conjugate(normalize(old.rotation))));
		for (int i = root; i < r.count; ++i)
			if (descendant(i, root, r))
				pose[i] = transformed(pose[i], old.position, target.position, delta);
	}
	template <class Pose>
	inline void fingers(const rig& r,
	                    const pose_library& library,
	                    const Pose& grip,
	                    std::span<const joint_pose> joints,
	                    int hand,
	                    std::span<bone> pose) noexcept
	{
		for (int i = 0; i < r.count; ++i)
		{
			const int index = library.finger[i];
			if (index < 0 || !descendant(i, r.arms[hand].wrist, r))
				continue;
			for (const auto& joint : joints)
				if (joint.name == grip.fingers[index].name)
				{
					move_part(r,
					          i,
					          compose(as_anchor(pose[r.parent[i]]),
					                  {library.rest_local[i].position, joint.rotation}),
					          pose);
					break;
				}
		}
	}
}
