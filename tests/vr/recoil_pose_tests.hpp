#pragma once
#include "component/vr/gameplay/weapon_recoil.hpp"

namespace recoil_pose_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::hands;
		using vr::gameplay::weapons::recoil::apply_to_pose;
		const std::array<vec,3> axis{{{1,0,0},{0,1,0},{0,0,1}}};
		const auto close=[](vec a,vec b){return length(sub(a,b))<.002f;};
		rig r{};r.count=11;r.gun=9;r.muzzle=10;
		r.arms={arm{1,2,3},arm{5,6,7}};
		const int parents[]{-1,0,1,2,3,0,5,6,7,0,9};
		std::copy(std::begin(parents),std::end(parents),r.parent.begin());
		r.weapon_bones[9]=r.weapon_bones[10]=true;
		for (int rear=0;rear<2;++rear) for (bool supported:{false,true}) for (float climb:{12.f,55.f,90.f})
		{
			std::array<bone,11> pose{};
			for(auto& b:pose)b.rotation={0,0,0,1};
			for(int h=0;h<2;++h)
			{
				const auto a=r.arms[h];const float side=h==0?1.f:-1.f;
				pose[a.shoulder].position={-5,side*5,5};
				pose[a.elbow].position={-2,side*7,0};
				pose[a.wrist].position={h==rear?0.f:8.f,0,0};
				pose[a.wrist+1].position=add(pose[a.wrist].position,{1,0,0});
			}
			pose[r.muzzle].position={10,0,0};
			const auto before=pose;
			apply_to_pose(r,pose,rear,supported?1-rear:-1,axis,climb);
			const auto wrist=r.arms[rear].wrist;
			const auto forward=rotate(pose[r.gun].rotation,{1,0,0});
			check(std::abs(std::atan2(forward[2],forward[0])*180.f/3.14159265358979323846f-
				std::min(climb,55.f))<.001f,"pose application respects the 55-degree cap even for oversized input");
			check(forward[2]>.1f && close(rotate(pose[wrist].rotation,{1,0,0}),forward),
				"either firing wrist pitches upward with the gun");
			check(close(pose[wrist].position,before[wrist].position) &&
				close(sub(pose[wrist+1].position,pose[wrist].position),forward),
				"firing wrist stays at the tracked pivot and fingers follow the grip");
			const auto off=r.arms[1-rear];
			if(supported)
			{
				check(close(pose[off.wrist].position,scale(forward,8)) &&
					close(rotate(pose[off.wrist].rotation,{1,0,0}),forward) &&
					close(sub(pose[off.wrist+1].position,pose[off.wrist].position),forward),
					"support wrist and fingers preserve their contact with the rising foregrip");
				check(close(pose[off.shoulder].position,before[off.shoulder].position) &&
					std::abs(length(sub(pose[off.elbow].position,pose[off.shoulder].position))-
						length(sub(before[off.elbow].position,before[off.shoulder].position)))<.002f &&
					std::abs(length(sub(pose[off.wrist].position,pose[off.elbow].position))-
						length(sub(before[off.wrist].position,before[off.elbow].position)))<.002f,
					"support elbow follows shared IK while reachable arm lengths and shoulder stay fixed");
			}
			else for(int i=off.shoulder;i<=off.wrist+1;++i)
				check(pose[i].position==before[i].position && pose[i].rotation==before[i].rotation,
					"single-hand recoil cannot move the free hand or the other weapon's arm");
			check(pose[r.arms[rear].elbow].position==before[r.arms[rear].elbow].position,
				"wrist pitch keeps the firing elbow connected");
			auto disabled=before;
			apply_to_pose(r,disabled,rear,supported?1-rear:-1,axis,0);
			for(int i=0;i<r.count;++i)
				check(disabled[i].position==before[i].position && disabled[i].rotation==before[i].rotation,
					"disabled or settled recoil leaves the complete pose unchanged");
		}
	}
}
