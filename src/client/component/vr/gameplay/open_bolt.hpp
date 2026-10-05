#pragma once
#include "action_state.hpp"

namespace vr::gameplay::weapons::open_bolt
{
	// No persistent chambered round: feeding and firing consume a magazine
	// round in one accepted native shot. Manual cocking never moves ammunition.
	struct rules { int magazine_capacity{}; };
	struct state
	{
		bool magazine_inserted{};
		int magazine_rounds{};
		action_state action{action_state::closed};
		bool operator==(const state&) const = default;
	};
	inline bool valid(const rules& r,const state& s) noexcept
	{
		return r.magazine_capacity>0 && r.magazine_capacity<=1000 &&
			s.magazine_rounds>=0 && s.magazine_rounds<=r.magazine_capacity &&
			(s.magazine_inserted || s.magazine_rounds==0) &&
			(s.action==action_state::closed || s.action==action_state::held_open || s.action==action_state::cocked_open);
	}
	inline bool ready(const rules& r,const state& s,bool manipulating=false) noexcept
	{ return valid(r,s) && s.action==action_state::cocked_open && s.magazine_inserted && s.magazine_rounds>0 && !manipulating; }
	inline bool fire(const rules& r,state& s) noexcept
	{
		if (!ready(r,s)) return false;
		--s.magazine_rounds;
		// Last accepted shot leaves the action closed, without a follower lock.
		s.action=s.magazine_rounds ? action_state::cocked_open : action_state::closed;
		return true;
	}
	inline bool dry_fire(const rules& r,state& s) noexcept
	{
		if (!valid(r,s) || s.action!=action_state::cocked_open || s.magazine_rounds!=0) return false;
		// An empty or absent magazine cannot stop the released bolt. This is
		// a trigger operation, never an accepted native shot or ammo change.
		s.action=action_state::closed;
		return true;
	}
	inline bool extract(const rules& r,state& s,int& extracted) noexcept
	{
		if (!valid(r,s) || s.action==action_state::held_open) return false;
		extracted=0; // Common stroke interface; this feed has no live extraction.
		s.action=action_state::held_open;
		return true;
	}
	inline bool finish_stroke(const rules& r,state& s) noexcept
	{
		if (!valid(r,s) || s.action!=action_state::held_open) return false;
		s.action=action_state::cocked_open;
		return true;
	}
	inline bool cycle(const rules& r,state& s,int& extracted) noexcept
	{
		if (!extract(r,s,extracted)) return false;
		return finish_stroke(r,s);
	}
	// Native idle admission only. A native-ready loaded weapon starts cocked;
	// its entire loaded total stays in the magazine. Never re-infer this state
	// on insertion, ammo pickup, tracking loss or returning to an owned weapon.
	inline bool from_native_automatic(const rules& r,int loaded,state& output) noexcept
	{
		const state next{true,loaded,loaded>0 ? action_state::cocked_open : action_state::closed};
		if (!valid(r,next)) return false;
		output=next;
		return true;
	}
}
