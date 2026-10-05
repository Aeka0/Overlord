#pragma once
#include "launcher_profile.hpp"
#include "weapon_holding.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include <cmath>

namespace vr::gameplay::weapons::launcher
{
	struct rocket_hold {hand actor{hand::none};bool approached{};};
	inline hands::vec tail_contact(const launcher_profile& p,hands::anchor rocket,float units)noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		if(!std::isfinite(units) || units<=0)return {INFINITY,INFINITY,INFINITY};
		const auto a=compose(rocket,{p.tail_start,{0,0,0,1}}).position;
		const auto b=compose(rocket,{p.tail_end,{0,0,0,1}}).position;
		const auto axis=sub(b,a);const float square=dot(axis,axis);
		const float t=square>1e-8f?std::clamp(dot(sub(p.load_mouth,a),axis)/square,0.f,1.f):0.f;
		return scale(sub(add(a,scale(axis,t)),p.load_mouth),1/units);
	}
	inline bool insertion_aligned(hands::vec delta,float alignment)noexcept
	{
		for(float x:delta)if(!std::isfinite(x))return false;
		return std::isfinite(alignment) && alignment>=.42261826f &&
			std::hypot(delta[1],delta[2])<=.075f;
	}
	// Cross a real front approach before seating. Starting with a new rocket
	// already overlapping the socket cannot manufacture an insertion.
	inline bool seated(rocket_hold& held,hands::vec delta,float alignment)noexcept
	{
		if(!valid_hand(held.actor) || !insertion_aligned(delta,alignment))return false;
		if(delta[0]>=.09f && delta[0]<=.5f)held.approached=true;
		return held.approached && delta[0]>=-.04f && delta[0]<=.035f;
	}
	template<class Commit> bool draw(const launcher_profile& p,rocket_hold& held,hand actor,ammunition::projection observed,Commit&& commit)
	{
		if(!p.manual_loading() || valid_hand(held.actor) || !valid_hand(actor) || observed.loaded!=0 || observed.reserve<=0)return false;
		if(!commit(ammunition::projection{0,observed.reserve-1}))return false;
		held={actor,false};return true;
	}
	template<class Commit> bool settle(rocket_hold& held,ammunition::projection observed,bool insert,Commit&& commit)
	{
		if(!valid_hand(held.actor))return true;
		if(observed.loaded<0 || observed.loaded>1 || observed.reserve<0 || observed.reserve>=1000000 || (insert && observed.loaded))return false;
		if(!commit(ammunition::projection{insert?1:observed.loaded,observed.reserve+(insert?0:1)}))return false;
		held={};return true;
	}
}
