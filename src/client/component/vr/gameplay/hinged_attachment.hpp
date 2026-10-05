#pragma once
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "hinge_travel.hpp"
#include "physical_reload_geometry.hpp"

namespace vr::gameplay::weapons::hinged_attachment
{
	using namespace hands;
	inline constexpr float stroke=1.570796327f;
	// Metres in the gun frame. Contact follows the displayed housing; the hinge
	// and wrist describe its flat, single-axis manipulation path analytically.
	struct path
	{
		anchor hinge{},wrist{},contact{};vec extension{.04f,0,0};
		anchor housing{};vec low{},high{};bool housing_valid{};
	};
	inline anchor at(const path& p,float amount) noexcept
	{
		const float half=stroke*(1-std::clamp(amount,0.f,1.f))*.5f;
		return hands::pose_math::compose(p.hinge,hands::pose_math::compose({{}, {0,0,std::sin(half),std::cos(half)}},p.wrist));
	}
	inline bool finite(anchor a) noexcept
	{
		for(float v:a.position)if(!std::isfinite(v) || std::abs(v)>1e6f)return false;
		float n{};for(float v:a.rotation){if(!std::isfinite(v))return false;n+=v*v;}
		return n>.99f && n<1.01f;
	}
	inline float acquisition_distance(const path& p,anchor wrist)noexcept
	{
		if(!finite(wrist) || !finite(p.hinge) || !finite(p.wrist) || !finite(p.contact))return INFINITY;
		for(float v:p.extension)if(!std::isfinite(v))return INFINITY;
		float distance=physical_reload::segment_distance(wrist.position,p.contact.position,add(p.contact.position,p.extension))/.065f;
		if(p.housing_valid)
		{
			const auto inverse=hands::pose_math::inverse(p.housing);
			const auto point=hands::pose_math::compose(inverse,wrist).position;
			const auto forward=rotate(inverse.rotation,p.extension);auto low=p.low,high=p.high;
			for(int i=0;i<3;++i){low[i]+=std::min(0.f,forward[i]);high[i]+=std::max(0.f,forward[i]);}
			distance=std::min(distance,physical_reload::box_distance(point,low,high)/.05f);
		}
		return distance;
	}
	class gesture
	{
		bool held_{},open_{},angle_valid_{};float amount_{},travel_{},angle_{},start_amount_{};anchor last_{};vec start_{};
	public:
		bool held() const noexcept{return held_;}bool open()const noexcept{return open_;}float amount()const noexcept{return amount_;}
		void reset(bool open)noexcept{held_=false;open_=open;amount_=travel_=open ? 1.f : 0.f;}
		void cancel()noexcept{reset(open_);}
		void release()noexcept{if(held_){open_=amount_>=.65f ? true : amount_<=.35f ? false : open_;reset(open_);}}
		bool acquire(const path& p,anchor wrist)noexcept
		{
			if(held_ || acquisition_distance(p,wrist)>1)return false;
			start_=hands::pose_math::compose(hands::pose_math::inverse(p.hinge),wrist).position;
			held_=true;last_=wrist;travel_=start_amount_=amount_;angle_=std::atan2(start_[1],start_[0]);
			angle_valid_=std::hypot(start_[0],start_[1])>=.01f;return true;
		}
		void move(const path& p,anchor wrist,float seconds,bool continuous=false)noexcept
		{
			if(!held_)return;
			// Continuous XR input can be consumed slowly during native breach.
			// Retain the bounded motion span and physical displacement checks.
			if(!finite(wrist) || !std::isfinite(seconds) || seconds<=0 || seconds>(continuous?.5f:.15f) || length(sub(wrist.position,last_.position))>.20f){cancel();return;}
			const auto local=rotate(conjugate(p.hinge.rotation),sub(wrist.position,p.hinge.position));
			// Use the actual lever arm selected anywhere on the housing. A fixed
			// offset to the animation's wrist under-rotates grips near the hinge.
			if(std::hypot(local[0],local[1])<.01f){angle_valid_=false;last_=wrist;return;}
			if(!angle_valid_){start_=local;travel_=start_amount_=amount_;angle_=std::atan2(local[1],local[0]);angle_valid_=true;last_=wrist;return;}
			advance_hinge_travel(travel_,angle_,std::atan2(local[1],local[0]),-stroke);
			const auto amount=std::clamp(travel_,0.f,1.f);
			const float half=-stroke*(amount-start_amount_)*.5f;
			const auto expected=rotate({0,0,std::sin(half),std::cos(half)},start_);
			if(length(sub(local,expected))>.12f){cancel();return;}
			amount_=amount;last_=wrist;
			if(amount_>.94f)open_=true;else if(amount_<.06f)open_=false;
		}
	};
}
