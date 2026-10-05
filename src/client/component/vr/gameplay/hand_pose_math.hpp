#pragma once
#include "hand_pose_library.hpp"

namespace vr::gameplay::hands::pose_math
{
	using namespace hands;
	inline anchor compose(anchor a, anchor b) noexcept
	{ return {add(a.position,rotate(a.rotation,b.position)),normalize(multiply(a.rotation,b.rotation))}; }
	inline anchor inverse(anchor a) noexcept
	{ const auto q=conjugate(normalize(a.rotation)); return {rotate(q,scale(a.position,-1)),q}; }
	// rigid_part retains source-model vertices. A desired bone pose therefore
	// needs the inverse source bind exactly once before native rigid submission.
	inline anchor rigid_delta(anchor desired,anchor source_bind) noexcept
	{ return compose(desired,inverse(source_bind)); }
	inline anchor as_anchor(const bone& b) noexcept { return {b.position,normalize(b.rotation)}; }
	inline void move_part(const rig& r,int root,anchor target,std::span<bone> pose) noexcept
	{
		const auto old=pose[root];
		const auto delta=normalize(multiply(target.rotation,conjugate(normalize(old.rotation))));
		for (int i=root;i<r.count;++i) if (descendant(i,root,r)) pose[i]=transformed(pose[i],old.position,target.position,delta);
	}
	template<class Pose>
	inline void fingers(const rig& r,const pose_library& library,const Pose& grip,
		std::span<const joint_pose> joints,int hand,std::span<bone> pose) noexcept
	{
		for (int i=0;i<r.count;++i)
		{
			const int index=library.finger[i];
			if (index<0 || !descendant(i,r.arms[hand].wrist,r)) continue;
			for (const auto& joint : joints) if (joint.name==grip.fingers[index].name)
			{
				move_part(r,i,compose(as_anchor(pose[r.parent[i]]),{library.rest_local[i].position,joint.rotation}),pose);
				break;
			}
		}
	}
}
