#pragma once
#include "component/vr/gameplay/forearm_twist.hpp"
#include <limits>

namespace forearm_twist_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::hands;using namespace std::chrono_literals;
		const auto turn=[](vec axis,float degrees){const float a=degrees*.00872664626f;axis=scale(unit(axis),std::sin(a));return quat{axis[0],axis[1],axis[2],std::cos(a)};};
		const auto same=[](quat a,quat b){return std::abs(multiply(normalize(a),conjugate(normalize(b)))[3])>.99999f;};
		rig r;r.parent.fill(-1);r.count=11;r.arms={arm{1,2,3},arm{6,7,8}};
		r.parent[1]=r.parent[6]=0;r.parent[2]=1;r.parent[3]=r.parent[4]=2;r.parent[5]=4;
		r.parent[7]=6;r.parent[8]=r.parent[9]=7;r.weapon_bones[10]=true;
		std::array<bone_definition,11> bones{};
		for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};bones[i].bind.weight=2;}
		bones[4].name="j_wristtwist_le";bones[9].name="j_wristtwist_ri";
		for(int h=0;h<2;++h)
		{
			const auto a=r.arms[h];const float y=h ? -2.f:2.f;const int node=h ? 9:4;
			bones[a.shoulder].bind.position={-5,y,0};bones[a.elbow].bind.position={0,y,0};bones[a.wrist].bind.position={10,y,0};
			bones[node].bind.position={7.35f,y,.2f};
			bones[a.elbow].bind.rotation=turn({1,0,0},h ? 30.f:-30.f);
			bones[a.wrist].bind.rotation=turn({0,0,1},h ? -40.f:40.f);
			bones[node].bind.rotation=turn({0,1,0},h ? -15.f:15.f);
		}
		bones[5].bind.position={7.35f,2,1};
		const model_definition model{"hands",0,10};const auto binding=bind_forearm_twist(r,bones,model);
		check(binding.arms[0].node==4 && binding.arms[1].node==9,"twist binds each elbow sibling and its own anatomical axes");
		std::array<bone,11> rest{};for(int i=0;i<r.count;++i)rest[i]=bones[i].bind;
		const auto epoch=forearm_twist::clock::time_point{1s};
		for(int h=0;h<2;++h)for(bool world:{false,true})
		{
			forearm_twist controller;const auto a=r.arms[h];const int node=h ? 9:4;
			const auto world_q=world ? turn({1,2,3},70):quat{0,0,0,1};const vec offset=world ? vec{50,-20,90}:vec{};
			int frame{};
			for(float degrees:{0.f,90.f,-90.f,179.f,181.f,270.f,360.f})
			{
				auto pose=rest;const auto roll=turn({1,0,0},degrees);
				pose[a.wrist].rotation=multiply(roll,rest[a.wrist].rotation);
				// Stale native auxiliary rotation must not survive finalization.
				pose[node].rotation=turn({1,1,1},45);
				for(auto& b:pose)b=transformed(b,{},offset,world_q);
				const auto before=pose;
				check(controller.update(binding,r,pose,1,epoch+(++frame)*10ms)==3,"both valid arm deformations finalize");
				check(same(pose[node].rotation,multiply(world_q,multiply(roll,rest[node].rotation))),"final anatomical wrist drives full axial twist through 180 degrees in either hand/world frame");
				check(pose[node].position==before[node].position,"twist preserves the solved cuff pivot");
				for(int i=0;i<r.count;++i)if(!binding.arms[0].affected[i] && !binding.arms[1].affected[i])
					check(pose[i].position==before[i].position && pose[i].rotation==before[i].rotation,"deformation cannot move hands, fingers, elbow, shoulder or weapon");
				const auto once=pose;controller.update(binding,r,pose,1,epoch+frame*10ms);
				for(int i=0;i<r.count;++i)check(same(pose[i].rotation,once[i].rotation) && length(sub(pose[i].position,once[i].position))<.0001f,"repeated finalization never accumulates twist");
				for(auto& x:pose[a.wrist].rotation)x=-x;controller.update(binding,r,pose,1,epoch+frame*10ms);
				check(same(pose[node].rotation,once[node].rotation),"quaternion sign is not an extra sleeve turn");
			}
			for(float degrees:{-80.f,-30.f,30.f,80.f})
			{
				auto pose=rest;pose[a.wrist].rotation=multiply(turn({0,1,0},degrees),rest[a.wrist].rotation);
				controller.update(binding,r,pose,1,epoch+(++frame)*10ms);
				check(same(pose[node].rotation,rest[node].rotation),"wrist flexion alone does not bend or twist the cuff");
			}
		}
		for(int reset=0;reset<5;++reset)
		{
			forearm_twist controller;auto pose=rest;pose[3].rotation=multiply(turn({1,0,0},90),rest[3].rotation);
			controller.update(binding,r,pose,1,epoch);const auto previous=pose[4].rotation;
			pose=rest;pose[3].rotation=multiply(turn({0,1,0},180),rest[3].rotation);
			if(reset==1)controller.reset();
			if(reset==4)controller.update(binding,r,pose,1,epoch+5ms,2);
			controller.update(binding,r,pose,reset==2 ? 2:1,epoch+(reset==3 ? 200ms:10ms));
			check(same(pose[4].rotation,reset ? rest[4].rotation:previous),"singular wrist bend holds valid twist only within the same tracked reference/lifetime");
		}
		for(int bad=0;bad<5;++bad)
		{
			auto definitions=bones;auto layout=r;
			if(bad==0)definitions[4].name="missing";
			if(bad==1)definitions[5].name="j_wristtwist_le";
			if(bad==2)definitions[4].parent=layout.parent[4]=3;
			if(bad==3)layout.weapon_bones[5]=true;
			if(bad==4)definitions[4].bind.rotation[0]=std::numeric_limits<float>::quiet_NaN();
			const auto optional=bind_forearm_twist(layout,definitions,model);
			check(optional.arms[0].node<0 && optional.arms[1].node==9,"missing/ambiguous/misparented/foreign/nonfinite helper falls back per arm");
		}
		{
			forearm_twist controller;auto pose=rest;pose[5].position[0]=std::numeric_limits<float>::infinity();
			pose[3].rotation=multiply(turn({1,0,0},90),rest[3].rotation);
			check(controller.update(binding,r,pose,1,epoch)==2 && pose[4].rotation==rest[4].rotation,"invalid child prevents a partial arm deformation while the other arm remains usable");
		}
	}
}
