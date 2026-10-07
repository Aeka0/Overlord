#pragma once
#include "grenade_policy.hpp"
#include <algorithm>
#include <cstdint>

namespace vr::gameplay::grenades
{
	enum class phase : unsigned { stowed, safe, prepared, cooking, release_pending };
	struct state
	{
		phase stage{}; kind type{}; vr::hand holder{vr::hand::none}, puller{vr::hand::none};
		std::uint32_t weapon{};std::uint64_t revision{}, reference{};
		int fuse_ms{}, deadline{}, remaining{};
		bool spent{};
		bool held()const noexcept{return stage!=phase::stowed;}
		bool live()const noexcept{return held() && spent;}
		bool take(kind k,std::uint32_t token,vr::hand h,std::uint64_t ref,int fuse) noexcept
		{
			if(held() || !valid(k) || !token || token>=512 || !vr::valid_hand(h) || !ref || fuse<0 || fuse>60000 ||
				(behaviors[unsigned(k)].timed ? fuse==0 : fuse!=0))return false;
			const auto rev=revision+1;*this={};revision=rev;stage=behaviors[unsigned(k)].pin_gesture?phase::safe:phase::prepared;type=k;weapon=token;holder=h;reference=ref;fuse_ms=fuse;return true;
		}
		void stow()noexcept{const auto rev=revision+1;*this={};revision=rev;}
		// Native pickup has already consumed the world missile. Its existing
		// deadline is authoritative; no chest debit, pin pull or new cook timer.
		bool take_live(kind k,std::uint32_t token,vr::hand h,std::uint64_t ref,int now,int expires)noexcept
		{
			if(!valid(k) || !behaviors[unsigned(k)].cook || now<0 || expires<=now ||
				std::int64_t(expires)-now>60000 || !take(k,token,h,ref,expires-now))return false;
			spent=true;stage=phase::cooking;deadline=expires;return true;
		}
		bool return_to_chest()noexcept
		{if(type!=kind::football || stage!=phase::prepared || spent)return false;stow();return true;}
		bool handoff(vr::hand next)noexcept
		{
			if(!held() || stage==phase::release_pending || vr::valid_hand(puller) || !vr::valid_hand(next) || next==holder)return false;
			holder=next;puller=vr::hand::none;++revision;return true;
		}
		// Caller must commit exactly one native ammunition debit first.
		bool pull_pin(bool debited)noexcept
		{if(stage!=phase::safe || !behaviors[unsigned(type)].pin_gesture || !debited)return false;spent=true;stage=phase::prepared;puller=vr::hand::none;return true;}
		bool cook(int time)noexcept
		{
			if(!behaviors[unsigned(type)].cook || stage!=phase::prepared || time<0 || time>INT32_MAX-fuse_ms)return false;
			deadline=time+fuse_ms;stage=phase::cooking;return true;
		}
		bool due(int time)const noexcept{return stage==phase::cooking && time>=deadline;}
		void release(int time)noexcept
		{
			if(stage==phase::safe){stow();return;}
			if(stage!=phase::prepared && stage!=phase::cooking)return;
			if(!behaviors[unsigned(type)].timed){remaining=deadline=0;stage=phase::release_pending;puller=vr::hand::none;return;}
			remaining=stage==phase::cooking?std::clamp(deadline-time,1,fuse_ms):fuse_ms;
			// Retain the deadline on failed spawn retries; retry must not reset a fuse.
			if(stage==phase::prepared)deadline=time<=INT32_MAX-fuse_ms?time+fuse_ms:INT32_MAX;
			stage=phase::release_pending;puller=vr::hand::none;
		}
		int fuse_at(int time)const noexcept{return !behaviors[unsigned(type)].timed?0:stage==phase::release_pending?std::clamp(deadline-time,1,fuse_ms):fuse_ms;}
	};
	class release_motion
	{
		struct point {hands::vec p{};controller_input::clock::time_point time{};};
		std::array<point,8> points{};unsigned count{};
	public:
		void reset()noexcept{count=0;}
		void sample(hands::vec p,controller_input::clock::time_point time,float units)noexcept
		{
			if(!std::isfinite(units) || units<=0){reset();return;}
			for(float x:p)if(!std::isfinite(x)){reset();return;}
			if(count && time==points[count-1].time)return;
			if(count && (time<points[count-1].time || time-points[count-1].time>std::chrono::milliseconds(150) ||
				hands::length(hands::sub(p,points[count-1].p))>units*.75f))reset();
			if(count==points.size()){std::move(points.begin()+1,points.end(),points.begin());--count;}
			points[count++]={p,time};
		}
		hands::vec velocity(controller_input::clock::time_point now,float units)const noexcept
		{
			if(count<2 || now<points[count-1].time || now-points[count-1].time>std::chrono::milliseconds(100))return {};
			unsigned first=count-2;
			while(first && points[count-1].time-points[first-1].time<=std::chrono::milliseconds(80))--first;
			const float dt=std::chrono::duration<float>(points[count-1].time-points[first].time).count();
			if(dt<.005f || !std::isfinite(units) || units<=0)return {};
			auto v=hands::scale(hands::sub(points[count-1].p,points[first].p),1/dt);
			const float speed=hands::length(v),limit=units*15.f;
			return speed>limit?hands::scale(v,limit/speed):v;
		}
	};
}
