#pragma once
#include "component/vr/gameplay/melee_motion.hpp"
#include "component/vr/gameplay/body_equipment.hpp"
#include <limits>

namespace melee_motion_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay;
		using namespace hands;
		using namespace std::chrono_literals;
		const auto initial=[](melee::tool kind)
		{
			melee::sample s;s.count=2;s.sequence=1;s.reference=1;s.identity=1;s.kind=kind;
			s.at=melee::clock::time_point{}+1s;s.points[1]={.7f,0,0};return s;
		};
		for (auto kind:{melee::tool::fist,melee::tool::firearm,melee::tool::shield})
		{
			melee::motion m;auto s=initial(kind);m.advance(s);
			++s.sequence;s.at+=20ms;for (auto& p:s.points)p[1]+=.045f;
			s.hand.position[1]+=.045f;
			check(!m.advance(s).armed,"ordinary 2.25 m/s repositioning must not arm a blunt strike");
		}
		{
			melee::motion m;auto s=initial(melee::tool::firearm);m.advance(s);
			++s.sequence;s.at+=50ms;for (auto& p:s.points)p[1]+=.6f;
			s.hand.position[1]+=.6f;
			check(m.advance(s).armed,"a continuous 60 cm fast gun strike must survive a 50 ms sampling interval");
		}
		for (auto interval:{10ms,20ms,50ms,100ms})
		{
			melee::motion m;auto s=initial(melee::tool::firearm);m.advance(s);
			bool armed{};
			for (auto elapsed=0ms;elapsed<100ms;elapsed+=interval)
			{
				const float travel=5.f*std::chrono::duration<float>(interval).count();
				++s.sequence;s.at+=interval;s.hand.position[1]+=travel;
				for (auto& p:s.points)p[1]+=travel;
				armed=m.advance(s).armed || armed;
			}
			check(armed,"the same half-metre strike arms across supported sampling intervals");
		}
		{
			melee::motion m;auto s=initial(melee::tool::firearm);
			s.points[0]={.025f,0,0};s.points[1]={.9f,0,0};m.advance(s);
			++s.sequence;s.at+=20ms;s.hand.rotation={0,0,std::sin(.2f),std::cos(.2f)};
			for (auto& p:s.points)p=rotate(s.hand.rotation,p);
			const auto swing=m.advance(s);
			check(swing.speed>12.f && swing.valid && swing.armed_at(1),"a fast rotating long-gun tip is not a tracking teleport");
			check(!swing.armed_at(0) && !swing.armed_at(9),"a fast muzzle does not lend damage eligibility to the slow stock or an invalid point");
			melee::hit_gate gate;
			check(!gate.contact(0,101,swing.armed_at(0),s.at) && gate.contact(1,202,swing.armed_at(1),s.at),
				"a slow contact cannot consume the shared damage cooldown before a qualified tip contact");
			const auto a=swing.from.points[1],b=swing.to.points[1];
			const float fraction=(.15f-a[1])/(b[1]-a[1]);
			check(fraction>0 && fraction<1 && swing.armed_at(1),"the full fast sweep retains a target crossed between samples");
		}
		{
			melee::motion m;auto s=initial(melee::tool::fist);m.advance(s);
			++s.sequence;s.at+=20ms;s.hand.position[1]=.28f;
			for (auto& p:s.points)p[1]+=.28f;
			check(m.advance(s).armed,"fast hand translation above the old 12 m/s limit remains eligible below the tracking guard");
		}
		{
			melee::motion m;auto s=initial(melee::tool::firearm);m.advance(s);
			for (int n=0;n<20;++n)
			{
				const float travel=n%2 ? -.05f : .05f;
				++s.sequence;s.at+=10ms;s.hand.position[1]+=travel;
				for (auto& p:s.points)p[1]+=travel;
				check(!m.advance(s).armed,"rapid five-centimetre oscillations cannot accumulate into a gun strike");
			}
		}
		{
			melee::motion m;auto s=initial(melee::tool::firearm);m.advance(s);
			++s.sequence;s.at+=10ms;s.points[0][1]+=.05f;
			check(!m.advance(s).armed,"short receiver movement is below useful travel");
			++s.sequence;s.at+=10ms;s.points[1][1]+=.08f;
			check(!m.advance(s).armed,"different fast points cannot pool their travel into a strike");
		}
		for (int boundary=0;boundary<8;++boundary)
		{
			melee::motion m;auto s=initial(melee::tool::firearm);m.advance(s);
			++s.sequence;s.at+=20ms;s.hand.position[1]+=.08f;
			for (auto& p:s.points)p[1]+=.08f;
			m.advance(s);
			++s.sequence;s.at+=20ms;s.hand.position[1]+=.08f;
			for (auto& p:s.points)p[1]+=.08f;
			switch(boundary)
			{
			case 0:++s.reference;break;
			case 1:++s.identity;break;
			case 2:++s.weapon;break;
			case 3:++s.continuity;break;
			case 4:++s.pose_revision;break;
			case 5:s.at+=150ms;break;
			case 6:s.at-=1s;break;
			case 7:s.sequence=1;break;
			}
			check(!m.advance(s).valid,"interruption, stale input, handoff and clock/sequence rollback cannot bridge a stroke");
			++s.sequence;s.at+=20ms;s.hand.position[1]+=.08f;
			for (auto& p:s.points)p[1]+=.08f;
			check(!m.advance(s).armed,"fresh short movement does not inherit travel from before an interruption");
		}
		for (int fault=0;fault<5;++fault)
		{
			melee::motion m;auto s=initial(melee::tool::firearm);m.advance(s);
			++s.sequence;s.at+=10ms;for (auto& p:s.points)p[1]+=.2f;
			switch(fault)
			{
			case 0:s.hand.position[1]=.8f;break;
			case 1:s.hand.rotation={0,0,1,0};break;
			case 2:s.hand.position[0]=std::numeric_limits<float>::infinity();break;
			case 3:s.hand.rotation={0,0,0,0};break;
			case 4:s.points[1][0]=std::numeric_limits<float>::quiet_NaN();break;
			}
			check(!m.advance(s).valid,"translation/rotation tracking jumps and malformed samples remain unable to hit");
		}
		{
			melee::motion m;auto s=initial(melee::tool::firearm);m.advance(s);
			++s.sequence;s.at+=10ms;s.hand.rotation={0,0,0,-1};
			check(m.advance(s).valid,"equivalent quaternion signs do not look like a rotation jump");
			check(!m.advance(s).valid,"a repeated input sample cannot replay a sweep");
		}
		{
			// The production hand witness uses the same body frame as contact points.
			vr::head_pose_bridge::spatial_frame body;body.units_per_meter=40;
			body.head_yaw_axis={vec{1,0,0},vec{0,1,0},vec{0,0,1}};
			const vec local{.3f,.1f,-.2f};
			const auto sample=[&]
			{
				auto s=initial(melee::tool::firearm);
				const auto yaw=from_axis(body.head_yaw_axis);
				const anchor wrist{equipment::body_world(body,local),yaw};
				s.hand={equipment::body_local(body,wrist.position),normalize(multiply(conjugate(yaw),wrist.rotation))};
				s.points[0]=s.hand.position;s.points[1]=add(s.points[0],vec{.7f,0,0});return s;
			};
			melee::motion m;m.advance(sample());body.head_position={100,200,300};
			body.head_yaw_axis={vec{0,1,0},vec{-1,0,0},vec{0,0,1}};
			auto s=sample();++s.sequence;s.at+=20ms;
			const auto swing=m.advance(s);
			check(swing.valid && !swing.armed && swing.speed<.001f,"player translation and snap turn neither arm nor invalidate a body-relative blunt stroke");
		}
	}
}
