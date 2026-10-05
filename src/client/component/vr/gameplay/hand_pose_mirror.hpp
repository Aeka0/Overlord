#pragma once
#include "hand_pose_math.hpp"

namespace vr::gameplay::hands::pose_mirror
{
	inline hands::vec position(hands::vec p) noexcept {p[1]=-p[1];return p;}
	inline hands::quat rotation(hands::quat q) noexcept {return {-q[0],q[1],-q[2],q[3]};}
	inline hands::anchor wrist(hands::anchor source,hands::quat basis,hands::vec contact={}) noexcept
	{
		return {hands::add(contact,position(hands::sub(source.position,contact))),
			hands::normalize(hands::multiply(rotation(source.rotation),basis))};
	}
	inline hands::vec local_point(hands::vec source,hands::quat basis) noexcept
	{return hands::rotate(hands::conjugate(basis),position(source));}
	// Use the selected hand fit, then preserve its actual contact unless the
	// profile explicitly witnesses a symmetric pair of real handles.
	template<class Part>
	inline Part part(const Part& source,hands::quat basis) noexcept
	{
		auto out=source;
		if(source.opposite_pose)
		{
			out.wrist=source.opposite_pose->wrist;out.contact_in_wrist=source.opposite_pose->contact_in_wrist;
			out.fingers=source.opposite_pose->fingers;
		}
		const auto contact=pose_math::compose(out.wrist,{out.contact_in_wrist,{0,0,0,1}}).position;
		const auto centre=out.symmetry_center ? *out.symmetry_center : contact;
		out.wrist=wrist(out.wrist,basis,centre);
		out.contact_in_wrist=local_point(out.contact_in_wrist,basis);
		out.opposite_pose.reset();
		return out;
	}
	// The magazine/loader remains a proper rigid object with its original axes.
	// Mirror the hand around its attachment, then recover the object-in-hand pose.
	inline hands::anchor object_in_wrist(hands::anchor object,hands::anchor in_wrist,hands::quat basis) noexcept
	{
		const auto source=pose_math::compose(object,pose_math::inverse(in_wrist));
		return pose_math::compose(pose_math::inverse(wrist(source,basis,object.position)),object);
	}
	inline hands::quat local_rotation(const hands::pose_library& library,int bone,int parent,hands::quat source) noexcept
	{
		return hands::normalize(hands::multiply(hands::multiply(hands::conjugate(library.mirror_basis[parent]),
			rotation(source)),library.mirror_basis[bone]));
	}
	template<class Pose>
	inline void fingers(const hands::rig& rig,const hands::pose_library& library,const Pose& grip,
		std::span<const joint_pose> source,int destination,std::span<hands::bone> solved,bool mirrored=true) noexcept
	{
		if (!mirrored) {pose_math::fingers(rig,library,grip,source,destination,solved);return;}
		std::array<joint_pose,64> poses{};std::size_t count{};
		for (int bone=0;bone<rig.count;++bone)
		{
			const auto other=library.opposite[bone],index=library.finger[bone],parent=rig.parent[bone];
			if (index<0 || other<0 || parent<0 || library.finger[other]<0 ||
				!hands::descendant(bone,rig.arms[destination].wrist,rig)) continue;
			for (const auto& joint:source) if (joint.name==grip.fingers[library.finger[other]].name && count<poses.size())
			{poses[count++]={grip.fingers[index].name,local_rotation(library,bone,parent,joint.rotation)};break;}
		}
		pose_math::fingers(rig,library,grip,{poses.data(),count},destination,solved);
	}
}
