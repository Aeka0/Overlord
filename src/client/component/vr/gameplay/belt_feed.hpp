#pragma once
#include <cmath>

namespace vr::gameplay::weapons::belt_feed
{
	// Feed access is independent of the open-bolt sear and the ammunition ledger.
	// Removing the box withdraws its entire belt; there is no second ammo owner.
	struct state { float cover{}; bool laid{}; float bridge{}; };
	inline constexpr float bridge_clearance=.9f;
	inline bool cover_available(state s,bool bridge)noexcept{return !bridge || s.bridge>=bridge_clearance;}
	inline bool valid(state s,bool box,int rounds,bool bridge=false)noexcept
	{return std::isfinite(s.cover) && s.cover>=0 && s.cover<=1 && (!s.laid || (box && rounds>0)) &&
		std::isfinite(s.bridge) && s.bridge>=0 && s.bridge<=1 && (bridge ? (s.cover==0 || cover_available(s,true)):s.bridge==0);}
	inline bool accessible(state s)noexcept{return s.cover>=.8f;}
	inline bool ready(state s)noexcept{return s.cover==0 && s.laid;}
	inline float settled_cover(float amount)noexcept{return amount<.03f ? 0.f : amount>.97f ? 1.f : amount;}
	inline bool move_cover(state& s,float amount,bool bridge=false)noexcept
	{
		if(!std::isfinite(amount) || amount<0 || amount>1)return false;
		amount=settled_cover(amount);
		if(amount>0 && !cover_available(s,bridge))return false;
		if(amount==s.cover)return false;s.cover=amount;return true;
	}
	inline bool move_bridge(state& s,float amount)noexcept
	{
		if(!std::isfinite(amount) || amount<0 || amount>1)return false;
		amount=settled_cover(amount);
		if(s.cover>0 && amount<bridge_clearance)return false;
		if(amount==s.bridge)return false;s.bridge=amount;return true;
	}
	inline bool lay(state& s,bool box,int rounds)noexcept
	{if(!accessible(s) || !box || rounds<=0 || s.laid)return false;s.laid=true;return true;}
	inline void remove_box(state& s)noexcept{s.laid=false;}
}
