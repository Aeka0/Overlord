#pragma once
#include "component/vr/gameplay/arm_clearance.hpp"

namespace arm_clearance_tests
{
	template<class Check>void run(Check check)
	{
		using namespace vr::gameplay::hands;
		const vec shoulder{},elbow{.25f,.2f,0},wrist{.5f,0,0},low{.15f,.10f,-.05f},high{.4f,.3f,.05f};
		const auto angle=arm_clearance::choose(shoulder,elbow,wrist,low,high,0);
		const auto moved=arm_clearance::swivel(shoulder,elbow,wrist,angle);
		check(arm_clearance::penetration(moved,wrist,low,high)<arm_clearance::penetration(elbow,wrist,low,high)*.1f,"elbow swivel clears forearm from box");
		check(std::abs(length(sub(moved,shoulder))-length(sub(elbow,shoulder)))<1e-6f &&
			std::abs(length(sub(moved,wrist))-length(sub(elbow,wrist)))<1e-6f,"clearance preserves both limb lengths");
		check(arm_clearance::choose(shoulder,elbow,wrist,{1,1,1},{2,2,2},0)==0,"clear arm keeps original elbow solution");
		for(float side:{1.f,-1.f})
		{
			const vec e{.25f,.2f*side,0},lo{.15f,side>0?.1f:-.3f,-.05f},hi{.4f,side>0?.3f:-.1f,.05f};
			const arm_clearance::preference natural{{0,side,0},{0,0,-1}};
			const auto a=arm_clearance::choose(shoulder,e,wrist,lo,hi,0,&natural);
			const auto result=arm_clearance::swivel(shoulder,e,wrist,a);
			check(result[2]<-.02f && result[1]*side>0,"either elbow avoids box toward its own outer/lower side");
			for(float f:{.25f,.5f,.75f,1.f})check(arm_clearance::swivel(shoulder,e,wrist,a*f)[2]<=.002f,"smoothed elbow path never lifts above initial elbow");
		}
		rig r{};r.count=5;r.parent.fill(-1);r.parent[1]=0;r.parent[2]=1;r.parent[3]=2;r.arms[0]={0,1,2};r.weapon_bones[4]=true;
		std::array<bone,5> base{{{{0,0,0,1},shoulder},{{0,0,0,1},elbow},{{0,0,0,1},wrist},{{0,0,0,1},{.52f,.01f,0}},{{0,0,0,1},{.3f,0,0}}}};
		arm_clearance::motion solver;auto now=std::chrono::steady_clock::time_point{}+std::chrono::seconds(1);
		std::array<bone,5> solved{};float previous{};
		for(int i=0;i<30;++i)
		{
			solved=base;now+=std::chrono::milliseconds(16);solver.apply(r,0,solved,{},low,high,1,1,now);
			check(std::abs(solver.angle-previous)<=.09601f,"elbow correction changes smoothly across frames");previous=solver.angle;
			for(int b:{2,3,4})check(solved[b].position==base[b].position && solved[b].rotation==base[b].rotation,"clearance leaves wrist, fingers and gun fixed");
		}
		check(length(sub(solved[1].position,elbow))>.02f,"persistent box collision produces visible elbow clearance");
		solver.reset();check(solver.angle==0,"ending support removes clearance lease");
		// A short, almost straight arm was clamped behind the foregrip, inside
		// the box. Swivelling alone cannot move this wrist out of the obstacle.
		for(int hand=0;hand<2;++hand)
		{
			r.arms[hand]={0,1,2};
			auto short_arm=base;short_arm[1].position={.25f,.002f,0};
			const vec contact{.56f,0,0},box_low{.40f,-.025f,-.025f},box_high{.51f,.025f,.025f};
			arm_clearance::motion contact_solver;vec previous_wrist=short_arm[2].position;
			for(int frame=0;frame<60;++frame)
			{
				solved=short_arm;now+=std::chrono::milliseconds(16);
				contact_solver.apply(r,hand,solved,{},box_low,box_high,1,2,now,&contact);
				check(length(sub(solved[2].position,previous_wrist))<=.0101f,"foregrip contact restoration blends instead of snapping");
				previous_wrist=solved[2].position;
				check(length(sub(solved[0].position,short_arm[0].position))<=.06001f,"virtual shoulder advance stays within six centimetres");
				check(solved[4].position==short_arm[4].position && solved[4].rotation==short_arm[4].rotation,"short-arm compensation never moves the gun");
				check(length(sub(sub(solved[3].position,solved[2].position),sub(short_arm[3].position,short_arm[2].position)))<1e-6f &&
					solved[2].rotation==short_arm[2].rotation,"wrist correction preserves finger contact and hand orientation");
			}
			check(length(sub(solved[2].position,contact))<1e-6f,"both short arms reach the actual foregrip instead of retreating into box");
			check(arm_clearance::penetration(solved[1].position,solved[2].position,box_low,box_high)<
				arm_clearance::penetration(short_arm[1].position,short_arm[2].position,box_low,box_high),"anchored wrist and elbow reduce short-arm box penetration");
			const auto settled=solved;
			solved=short_arm;now+=std::chrono::milliseconds(16);contact_solver.apply(r,hand,solved,{},box_low,box_high,1,2,now,&contact);
			check(length(sub(solved[0].position,settled[0].position))<1e-6f && length(sub(solved[1].position,settled[1].position))<1e-6f,
				"contact correction converges without accumulating arm stretch");
			contact_solver.reset();solved=short_arm;now+=std::chrono::milliseconds(16);
			contact_solver.apply(r,hand,solved,{},box_low,box_high,1,3,now);
			check(solved[2].position==short_arm[2].position && contact_solver.contact_blend==0,"released support cannot retain wrist correction");
		}
	}
}
