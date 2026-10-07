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
	struct release_impulse
	{
		vec velocity{},angular_velocity{}; // World units/s and world radians/s.
		vec pivot{}; // Body centre in the released object's local units.
	};
	inline anchor free_motion(anchor exit,vec velocity,const release_impulse& impulse,float seconds,float units)noexcept
	{
		exit=free_drop(exit,hands::add(velocity,impulse.velocity),seconds,units);
		const float speed=hands::length(impulse.angular_velocity);
		if(speed>1e-6f && seconds>0)
		{
			const auto offset=hands::rotate(exit.rotation,impulse.pivot);
			const auto axis=hands::scale(impulse.angular_velocity,std::sin(.5f*speed*seconds)/speed);
			const hands::quat turn{axis[0],axis[1],axis[2],std::cos(.5f*speed*seconds)};
			exit.rotation=hands::normalize(hands::multiply(turn,exit.rotation));
			exit.position=hands::add(exit.position,hands::sub(offset,hands::rotate(exit.rotation,impulse.pivot)));
		}
		return exit;
	}
	struct flight
	{
		anchor start{},exit{};
		vec rail{},velocity{};
		float rail_seconds{},units{};
		clock::time_point born{};
		bool detached{};
		release_impulse impulse{}; // Free-flight motion; a nonzero rail duration defers it until departure.
		bool valid()const noexcept
		{
			float square{};for(float x:start.rotation){if(!std::isfinite(x))return false;square+=x*x;}
			for(auto v:{start.position,rail,velocity,impulse.velocity,impulse.angular_velocity,impulse.pivot})
				for(float x:v)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
			return square>.5f && square<1.5f && std::isfinite(units) && units>0 && units<=10000 &&
				std::isfinite(rail_seconds) && (rail_seconds==0 || (rail_seconds>=.001f && rail_seconds<1)) && hands::length(rail)<=units &&
				hands::length(impulse.angular_velocity)<=64 && hands::length(impulse.pivot)<=units;
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
				return free_motion(release,speed,impulse,age-rail_seconds,units);
			}
			return free_motion(start,velocity,impulse,age,units);
		}
	};
}
