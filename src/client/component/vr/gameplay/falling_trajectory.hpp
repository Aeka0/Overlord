#pragma once
#include "hands/pose_math.hpp"
#include <chrono>

namespace vr::gameplay::motion
{
	using clock=std::chrono::steady_clock;
	using hands::anchor;using hands::vec;
	inline constexpr auto flight_lifetime=std::chrono::milliseconds(1200);
	inline anchor translate_local(anchor origin,vec translation) noexcept
	{origin.position=hands::add(origin.position,hands::rotate(origin.rotation,translation));return origin;}
	inline anchor free_drop(anchor exit,vec velocity,float seconds,float units) noexcept
	{exit.position=hands::add(exit.position,hands::scale(velocity,seconds));exit.position[2]-=.5f*9.81f*units*seconds*seconds;return exit;}
	struct flight
	{
		anchor start{},exit{};
		vec rail{},velocity{};
		float rail_seconds{},units{};
		clock::time_point born{};
		bool detached{};
		bool valid()const noexcept
		{
			float square{};for(float x:start.rotation){if(!std::isfinite(x))return false;square+=x*x;}
			for(auto v:{start.position,rail,velocity})for(float x:v)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
			return square>.5f && square<1.5f && std::isfinite(units) && units>0 && units<=10000 &&
				std::isfinite(rail_seconds) && (rail_seconds==0 || (rail_seconds>=.001f && rail_seconds<1)) && hands::length(rail)<=units;
		}
		bool alive(clock::time_point now)const noexcept{return valid() && now>=born && now-born<=flight_lifetime;}
		void advance(clock::time_point now,const anchor* attached=nullptr)noexcept
		{
			if(detached || rail_seconds<=0 || !alive(now))return;
			if(attached){auto candidate=*this;candidate.start=*attached;if(candidate.valid())start=*attached;}
			exit=translate_local(start,rail);
			velocity=hands::scale(hands::rotate(start.rotation,rail),1/rail_seconds);
			if(std::chrono::duration<float>(now-born).count()>=rail_seconds)detached=true;
		}
		anchor pose(clock::time_point now)const noexcept
		{
			const float age=std::max(0.f,std::chrono::duration<float>(now-born).count());
			if(rail_seconds>0)
			{
				if(age<rail_seconds)return translate_local(start,hands::scale(rail,age/rail_seconds));
				// Rendering may pass the rail deadline before the next simulation
				// publication. Continue analytically instead of freezing at the mouth.
				const auto release=detached?exit:translate_local(start,rail);
				const auto speed=detached?velocity:hands::scale(hands::rotate(start.rotation,rail),1/rail_seconds);
				return free_drop(release,speed,age-rail_seconds,units);
			}
			return free_drop(start,velocity,age,units);
		}
	};
}
