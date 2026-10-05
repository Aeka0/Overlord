#pragma once
#include "hand_pose_solver.hpp"
#include <chrono>
#include <cstdint>

namespace vr::gameplay::hands::arm_clearance
{
	// Visual arm constraints only; never feed the result back into interaction
	// queries. Work in metres in the obstacle's local frame.
	inline float penetration(vec elbow,vec wrist,vec low,vec high)noexcept
	{
		float cost{};
		for(int i=0;i<=10;++i)
		{
			const auto p=add(elbow,scale(sub(wrist,elbow),float(i)/10.f));float depth=1;
			for(int k=0;k<3;++k)depth=std::min(depth,std::min(p[k]-low[k],high[k]-p[k]));
			if(depth>0)cost+=depth*depth;
		}
		return cost;
	}
	inline vec swivel(vec shoulder,vec elbow,vec wrist,float angle)noexcept
	{
		const auto axis=scale(unit(sub(wrist,shoulder)),std::sin(angle*.5f));
		return add(shoulder,rotate({axis[0],axis[1],axis[2],std::cos(angle*.5f)},sub(elbow,shoulder)));
	}
	struct preference {vec outward,down;};
	inline float choose(vec shoulder,vec elbow,vec wrist,vec low,vec high,float previous,const preference* natural=nullptr)noexcept
	{
		const auto posture=[&](vec p) {
			if(!natural)return 0.f;
			const auto bend=unit(sub(p,add(shoulder,scale(unit(sub(wrist,shoulder)),dot(sub(p,shoulder),unit(sub(wrist,shoulder)))))));
			return (1-std::clamp(dot(bend,unit(add(natural->outward,natural->down))),-1.f,1.f))*2e-5f;
		};
		float best{},score=penetration(elbow,wrist,low,high);
		if(score<1e-9f)return 0;
		score+=posture(elbow)+previous*previous*1e-7f;
		for(int i=-24;i<=24;++i)
		{
			const float angle=float(i)*.087266463f; // bounded five-degree search, no iterative physics
			const auto candidate=swivel(shoulder,elbow,wrist,angle);
			// Do not escape the box by lifting the elbow. Body-relative axes keep
			// this rule anatomical when the receiver rolls or points up/down.
			bool raised=false;
			if(natural)for(float portion:{.25f,.5f,.75f,1.f})
				raised|=dot(sub(swivel(shoulder,elbow,wrist,angle*portion),elbow),natural->down)<-.002f;
			if(raised)continue;
			const auto cost=penetration(candidate,wrist,low,high)+posture(candidate)+
				angle*angle*1e-6f+(angle-previous)*(angle-previous)*1e-7f;
			if(cost<score){score=cost;best=angle;}
		}
		return best;
	}
	struct motion
	{
		float angle{},contact_blend{};vec shoulder_offset{};int owner{-1};
		std::uint64_t reference{};std::chrono::steady_clock::time_point at{};
		void reset()noexcept{*this={};}
		void apply(const rig& r,int hand,std::span<bone> pose,anchor obstacle,vec low,vec high,float units,
			std::uint64_t frame_reference,std::chrono::steady_clock::time_point now,const vec* support_wrist=nullptr,
			const std::array<vec,3>* body_axis=nullptr)noexcept
		{
			if(hand<0 || hand>1 || pose.size()<size_t(r.count) || !std::isfinite(units) || units<=0)return;
			const auto a=r.arms[hand];if(a.shoulder<0 || a.elbow<=a.shoulder || a.wrist<=a.elbow || a.wrist>=r.count)return;
			const auto q=conjugate(normalize(obstacle.rotation));
			const auto local=[&](vec v){return scale(rotate(q,sub(v,obstacle.position)),1/units);};
			const auto shoulder=pose[a.shoulder].position,elbow=pose[a.elbow].position,wrist=pose[a.wrist].position;
			low=sub(scale(low,1/units),{.035f,.035f,.035f});high=add(scale(high,1/units),{.035f,.035f,.035f});
			if(reference!=frame_reference || owner!=hand || now<at || now-at>std::chrono::milliseconds(150))
			{reset();at=now;reference=frame_reference;owner=hand;}
			const float dt=std::clamp(std::chrono::duration<float>(now-at).count(),0.f,.05f);at=now;
			auto start=local(shoulder),joint=local(elbow),end=local(wrist);
			if(support_wrist)
			{
				for(float v:*support_wrist)if(!std::isfinite(v))return;
				contact_blend=std::min(1.f,contact_blend+dt/.10f);
				end=add(end,scale(sub(local(*support_wrist),end),contact_blend));
				const float upper=length(sub(joint,start)),lower=length(sub(local(wrist),joint));
				if(upper<1e-5f || lower<1e-5f)return;
				const auto direction=unit(sub(end,start));
				// An arm at maximum reach has no swivel circle. A small virtual
				// shoulder advance restores bend room instead of retracting the
				// already-gripped hand into the box. Cap this clavicle compensation.
				const bool obstructed=penetration(joint,local(wrist),low,high)>1e-9f || length(sub(end,local(wrist)))>.001f;
				const auto desired=obstructed ? scale(direction,std::clamp(length(sub(end,start))-.98f*(upper+lower),0.f,.06f)) : vec{};
				const auto step=sub(desired,shoulder_offset);const auto travel=length(step);
				if(travel>0)shoulder_offset=add(shoulder_offset,scale(step,std::min(1.f,.6f*dt/travel)));
				start=add(start,shoulder_offset);
				const auto reach=length(sub(end,start));if(reach<1e-5f)return;
				const auto axis=unit(sub(end,start));
				// Like mechanical part-hand constraints, exact contact wins when
				// the authored arm cannot reach even with shoulder compensation.
				// Derive extension from this frame's base pose, never accumulate it.
				const float extension=std::max(1.f,(reach+.0001f)/(upper+lower));
				const float u=upper*extension,l=lower*extension;
				const float along=std::clamp((u*u-l*l+reach*reach)/(2*reach),0.f,reach);
				auto pole=sub(sub(joint,start),scale(axis,dot(sub(joint,start),axis)));
				if(length(pole)<1e-5f)pole=cross(axis,std::abs(axis[2])<.9f ? vec{0,0,1}:vec{0,1,0});
				joint=add(start,add(scale(axis,along),scale(unit(pole),std::sqrt(std::max(0.f,u*u-along*along)))));
			}
			else {contact_blend=0;shoulder_offset={};}
			const preference natural{body_axis ? rotate(q,scale((*body_axis)[1],hand==0?1.f:-1.f)):vec{},
				body_axis ? rotate(q,scale((*body_axis)[2],-1)):vec{}};
			const auto target=choose(start,joint,end,low,high,angle,body_axis ? &natural:nullptr);
			angle+=std::clamp(target-angle,-6.f*dt,6.f*dt);
			joint=swivel(start,joint,end,angle);
			const auto world=[&](vec v){return add(obstacle.position,rotate(normalize(obstacle.rotation),scale(v,units)));};
			start=world(start);joint=world(joint);end=world(end);
			const auto rotations=orient_arm(sub(elbow,shoulder),sub(wrist,elbow),sub(joint,start),sub(end,joint),pose[a.shoulder].rotation);
			const auto upper=rotations.upper,lower=rotations.lower;
			for(int i=0;i<r.count;++i)if(!r.weapon_bones[i] && descendant(i,a.shoulder,r))
			{
				if(descendant(i,a.wrist,r)) {if(support_wrist)pose[i].position=add(pose[i].position,sub(end,wrist));}
				else pose[i]=descendant(i,a.elbow,r) ? transformed(pose[i],elbow,joint,lower) : transformed(pose[i],shoulder,start,upper);
			}
		}
	};
}
