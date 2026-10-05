#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/hand_contact.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "component/vr/gameplay/weapons/hand_poses/right_handle.hpp"

namespace hand_contact_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::hands;
		const auto& fingers=vr::gameplay::weapons::mp5::idle_fingers;
		rig r{}; r.count=2+int(fingers.size()); r.parent.fill(-1); r.arms[0].wrist=0; r.arms[1].wrist=1;
		std::array<bone_definition,38> bones{}; std::array<bone,38> pose{};
		for (auto& b:pose) b.rotation={0,0,0,1};
		for (size_t n=0;n<fingers.size();++n)
		{
			bones[n+2].name=fingers[n].name;
			r.parent[n+2]=fingers[n].name.find("_le")!=std::string_view::npos ? 0 : 1;
		}
		bones[0].name="j_wrist_le";bones[1].name="j_wrist_ri";
		for(auto& b:bones)b.bind.rotation={0,0,0,1};
		vr::gameplay::weapons::profile profile{};profile.fingers=fingers;
		const auto library=bind_weapon_poses(r,bones,profile);
		check(library.valid,"anatomical mirror fixture binds both complete hands");
		for(int n=0;n<r.count;++n)
		{
			const int other=library.opposite[n];
			check(other>=0 && library.opposite[other]==n,"every wrist/finger has a reciprocal mirror, including right ring and ring-palm names");
		}
		const auto& grasp=vr::gameplay::weapons::hand_poses::right_handle::fingers;
		vr::gameplay::hands::pose_mirror::fingers(r,library,profile,grasp,1,pose);
		for(const auto& joint:grasp)
		{
			int left=-1;for(int n=2;n<r.count;++n)if(bones[n].name==joint.name)left=n;
			check(left>=0 && library.opposite[left]>=0,"shared handle grasp includes only existing, paired finger joints");
			if(left<0 || library.opposite[left]<0)continue;
			const auto actual=pose[library.opposite[left]].rotation;
			const auto expected=vr::gameplay::hands::pose_mirror::local_rotation(library,library.opposite[left],1,joint.rotation);
			float dot{};for(size_t j=0;j<4;++j)dot+=actual[j]*expected[j];
			check(std::abs(dot)>.9999f,"mirroring applies every shared grasp joint, including the whole ring finger");
		}
		for(auto& b:pose)b.rotation={0,0,0,1};
		const auto binding=bind_contacts(r,bones,0);
		check(binding.valid,"all native left glove fingers bind to anatomical contact points");
		if (!binding.valid) return;
		for (size_t n=0;n<binding.fingers.size();++n) pose[binding.fingers[n]].position={2.f+float(n%3),float(n/3)-2,0};
		const anchor raw{{20,-30,10},normalize({.3f,.2f,-.4f,.8f})};
		std::array<vec,hand_contact_count> points{},rebased{};
		check(contact_points(binding,pose,raw,points),"full glove rebases onto the raw controller wrist");
		check(length(sub(points[15],add(raw.position,rotate(raw.rotation,{4.75f,-2,0}))))<1e-5f,
			"contact geometry extends past the last joint to the fingertip");
		check(length(sub(points[20],points[17]))>3,"palm and fingertips span distinct regions of the glove");
		const quat snap=normalize({-.3f,.4f,.2f,.7f});
		for (auto& b:pose) { b.position=add({400,-200,700},rotate(snap,b.position)); b.rotation=snap; }
		check(contact_points(binding,pose,raw,rebased),"a snapped visual hand still yields raw wrist contact geometry");
		for (size_t n=0;n<points.size();++n)
			check(length(sub(points[n],rebased[n]))<.0002f,"visual IK translation and rotation cannot manufacture a slap");
		auto bad=bones; bad[binding.fingers[14]].name="missing_tip_joint";
		check(!bind_contacts(r,bad,0).valid,"missing glove chain fails contact admission");
		auto wrong=r; wrong.weapon_bones[binding.fingers[0]]=true;
		check(!bind_contacts(wrong,bones,0).valid,"weapon bones cannot substitute for glove contact joints");
		pose[binding.wrist].rotation={};
		check(!contact_points(binding,pose,raw,rebased),"invalid wrist transform cannot produce slap contacts");
	}
}
