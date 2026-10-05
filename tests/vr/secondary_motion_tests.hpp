#pragma once
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/secondary_motion.hpp"
#include <limits>

namespace secondary_motion_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::hands;
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands::pose_math;
		namespace motion=secondary_motion;
		const auto& profile=mp5::strap;
		rig r{}; r.gun=0; r.count=7; r.parent.fill(-1);
		std::array<bone_definition,7> bones{};
		constexpr std::string_view names[]{"j_gun","j_front_strap_base","j_front_strap","j_front_strap_mid","j_front_strap_end","tag_clip","j_reload"};
		constexpr int parents[]{-1,0,1,2,3,0,0};
		std::array<bone,7> rest{};
		for (int i=0;i<r.count;++i)
		{
			r.parent[i]=parents[i]; r.weapon_bones[i]=true; bones[i].name=names[i]; rest[i].rotation={0,0,0,1};
			anchor local{{float(i),1,-2},{0,0,0,1}};
			for (const auto& part:mp5::equip_rest) if (part.name==names[i]) local=part.local;
			const auto pose=i ? compose(as_anchor(rest[parents[i]]),local) : anchor{{},{0,0,0,1}};
			rest[i].position=pose.position; rest[i].rotation=pose.rotation;
		}
		const auto binding=motion::bind(profile,r,bones);
		check(binding.valid,"native three-link MP5K front strap binds beneath its rigid mount");
		if (!binding.valid) return;
		auto bad=bones; bad[3].name="missing_strap_mid";
		check(!motion::bind(profile,r,bad).valid,"partial strap chains are rejected atomically");
		auto wrong=r; wrong.parent[4]=1;
		check(!motion::bind(profile,wrong,bones).valid,"strap motion cannot bind a broken parent chain");
		const auto at=[](int frame,int hz) { return motion::chain::clock::time_point{std::chrono::seconds(1)+
			std::chrono::duration_cast<motion::chain::clock::duration>(std::chrono::duration<double>(double(frame)/hz))}; };
		std::array<vec,4> settled{};
		for (int hz:{60,90,120})
		{
			motion::chain state; auto pose=rest;
			for (int frame=0;frame<=hz*3;++frame)
			{
				pose=rest;
				check(state.update(profile,binding,r,pose,{},39.37007874f,1,1,frame+1,at(frame,hz),true),"valid strap frame solves");
				for (int i:{0,1,5,6}) check(pose[i].position==rest[i].position && pose[i].rotation==rest[i].rotation,
					"strap motion leaves receiver, fixed ring, magazine and charging handle untouched");
				for (size_t n=0;n<binding.count;++n)
				{
					const int i=binding.bones[n];
					check(std::abs(length(sub(pose[i].position,pose[r.parent[i]].position))-length(profile.links[n].rest.local.position))<1e-5f,
						"strap links keep fixed length without detaching from their parent");
					for (int axis=0;axis<3;++axis)
						check(std::isfinite(state.angles()[n][axis]) && state.angles()[n][axis]>=profile.links[n].lower[axis] &&
							state.angles()[n][axis]<=profile.links[n].upper[axis],"strap remains finite and inside authored angular limits");
				}
			}
			check(length(sub(pose[4].position,rest[4].position))>.01f,"gravity relaxes the nylon chain from its static native pose");
			if (hz==60) settled=state.angles();
			else for (size_t n=0;n<binding.count;++n) check(length(sub(settled[n],state.angles()[n]))<.005f,"strap settling is stable across display rates");
			const auto once=pose; const auto angles=state.angles(); pose=rest;
			check(state.update(profile,binding,r,pose,{},39.37007874f,1,1,hz*3+1,at(hz*3,hz),true) && state.angles()==angles,
				"second eye or repeated input does not integrate strap physics again");
			for (int i=0;i<r.count;++i) check(length(sub(pose[i].position,once[i].position))<1e-5f,"duplicate input reproduces the same solved chain");
			for (int reset=0;reset<5;++reset)
			{
				auto interrupted=state; pose=rest;
				const auto offset=reset==3 ? vec{100,0,0} : vec{};
				check(interrupted.update(profile,binding,r,pose,offset,39.37007874f,reset==0 ? 2 : 1,reset==1 ? 2 : 1,
					reset==4 ? 1 : hz*3+2,at(hz*3+(reset==2 ? hz : 1),hz),true),"tracking or ownership discontinuity safely restarts strap motion");
				check(interrupted.angles()==std::array<vec,4>{},"recenter, swap, long pause, teleport and sequence regression discard old strap velocity");
			}
		}
		motion::chain moving,stationary; auto moving_pose=rest,stationary_pose=rest;
		for (int frame=0;frame<=30;++frame)
		{
			moving_pose=stationary_pose=rest;
			const float t=float(frame)/90;
			moving.update(profile,binding,r,moving_pose,{0,100*t*t,0},39.37007874f,1,1,frame+1,at(frame,90),true);
			stationary.update(profile,binding,r,stationary_pose,{},39.37007874f,1,1,frame+1,at(frame,90),true);
		}
		check(length(sub(moving_pose[4].position,stationary_pose[4].position))>.005f,"accelerating the weapon produces inertial strap motion");
		for (int invalid=0;invalid<3;++invalid)
		{
			auto pose=rest; if (invalid==1) pose[3].rotation={}; const auto before=pose;
			check(!moving.update(profile,binding,r,pose,{},invalid==0 ? 0.f : 39.37007874f,1,1,33,at(33,90),invalid!=2),
				"invalid scale, malformed bone and inactive tracking fail closed");
			check(moving.angles()==std::array<vec,4>{},"invalid motion resets its transient state");
			for (int i=0;i<r.count;++i) check(pose[i].position==before[i].position && pose[i].rotation==before[i].rotation,
				"invalid motion never publishes a partly moved chain");
		}
	}
}
