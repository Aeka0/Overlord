#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>

namespace vr::body_pose
{
	using vec=std::array<float,3>;
	using axes=std::array<vec,3>;
	struct estimate {bool valid{};vec position{};axes yaw_axis{};};
	inline estimate to_world(const estimate& local,vec origin,const axes& basis,float units) noexcept
	{
		if(!local.valid)return {};
		estimate out{true,origin,{}};
		for(unsigned world=0;world<3;++world)for(unsigned i=0;i<3;++i)
		{
			out.position[world]+=local.position[i]*units*basis[i][world];
			for(unsigned row=0;row<3;++row)out.yaw_axis[row][world]+=local.yaw_axis[row][i]*basis[i][world];
		}
		return out;
	}
	// Equipment anchor at nominal head height, not tracked hips/shoulders.
	// State lives in recenter-relative metres: locomotion and artificial turns
	// are applied outside the estimator and therefore never acquire filter lag.
	class estimator
	{
		using clock=std::chrono::steady_clock;
		bool valid_{};std::uint64_t reference_{};clock::time_point at_{};
		vec position_{},eye_reference_{},last_head_{};float yaw_{};
		static float wrap(float x) noexcept {return std::remainder(x,360.f);}
	public:
		void reset() noexcept {*this={};}
		estimate update(vec head,const axes& orientation,float head_yaw,std::uint64_t reference,clock::time_point at) noexcept
		{
			for(float x:head)if(!std::isfinite(x) || std::abs(x)>10000){reset();return {};}
			for(auto row:orientation)for(float x:row)if(!std::isfinite(x) || std::abs(x)>1.001f){reset();return {};}
			for(unsigned i=0;i<3;++i)for(unsigned j=i;j<3;++j)
			{float dot{};for(unsigned k=0;k<3;++k)dot+=orientation[i][k]*orientation[j][k];if(std::abs(dot-(i==j?1.f:0.f))>.002f){reset();return {};}}
			const auto& forward=orientation[0];const auto& left=orientation[1];const auto& up=orientation[2];
			if((forward[1]*left[2]-forward[2]*left[1])*up[0]+(forward[2]*left[0]-forward[0]*left[2])*up[1]+
				(forward[0]*left[1]-forward[1]*left[0])*up[2]<.998f){reset();return {};}
			if(!reference || !std::isfinite(head_yaw)){reset();return {};}
			const vec eye{orientation[0][0]*.08f,orientation[0][1]*.08f,orientation[0][2]*.08f};
			float jump{};for(unsigned i=0;i<3;++i)jump+=(head[i]-last_head_[i])*(head[i]-last_head_[i]);
			if(!valid_ || reference_!=reference || at<at_ || at-at_>std::chrono::milliseconds(150) || jump>.75f*.75f)
			{position_=head;eye_reference_=eye;yaw_=wrap(head_yaw);reference_=reference;valid_=true;at_=at;}
			else if(at>at_)
			{
				const float dt=std::chrono::duration<float>(at-at_).count();
				vec neck{};for(unsigned i=0;i<3;++i)neck[i]=head[i]-eye[i]+eye_reference_[i];
				const float dx=neck[0]-position_[0],dy=neck[1]-position_[1],distance=std::hypot(dx,dy);
				const float blend=1-std::exp(-dt/.12f);
				if(distance>.07f)
				{
					const float movement=std::max((distance-.07f)*blend,distance-.15f);
					position_[0]+=dx/distance*movement;position_[1]+=dy/distance*movement;
				}
				// Follow physical crouching; camera/native crouch already lives in
				// the unfiltered world base. Neck compensation reduces nod motion.
				position_[2]+=(neck[2]-position_[2])*(1-std::exp(-dt/.08f));
				const float angle=wrap(head_yaw-yaw_);
				if(std::abs(angle)>35.f)
				{
					const float excess=angle-std::copysign(35.f,angle);
					yaw_=wrap(yaw_+std::clamp(excess*(1-std::exp(-dt/.18f)),-180.f*dt,180.f*dt));
				}
				at_=at;
			}
			last_head_=head;
			const float radians=yaw_*.01745329252f,c=std::cos(radians),s=std::sin(radians);
			return {true,position_,{vec{c,s,0},vec{-s,c,0},vec{0,0,1}}};
		}
	};
}
