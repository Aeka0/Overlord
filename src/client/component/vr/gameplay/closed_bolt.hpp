#pragma once
#include "action_state.hpp"
#include <algorithm>
#include <cstdint>

namespace vr::gameplay::weapons::closed_bolt
{
	// Feed/chamber mechanics, independent of controls, native playerState,
	// magazine ownership, animations and a particular weapon's capacity.
	using weapons::action_state;
	struct rules
	{
		int magazine_capacity{};
		bool last_round_lock{}, plus_one{};
		bool manual_catch{};
	};
	struct state
	{
		bool magazine_inserted{}, chamber_loaded{};
		int magazine_rounds{};
		action_state action{action_state::closed};
		bool operator==(const state&) const = default;
	};
	inline bool valid(const rules& r, const state& s) noexcept
	{
		return r.magazine_capacity > 0 && r.magazine_capacity <= 1000 &&
			s.magazine_rounds >= 0 && s.magazine_rounds <= r.magazine_capacity &&
			(s.magazine_inserted || s.magazine_rounds == 0) &&
			(r.plus_one || s.magazine_rounds + int(s.chamber_loaded) <= r.magazine_capacity) &&
			(s.action == action_state::closed || (s.action == action_state::held_open && !s.chamber_loaded) ||
			 (s.action == action_state::locked_open && r.last_round_lock && !s.chamber_loaded) ||
			 (s.action == action_state::latched_open && r.manual_catch && !s.chamber_loaded));
	}
	inline bool ready(const rules& r, const state& s, bool manipulating = false) noexcept
	{
		return valid(r, s) && s.chamber_loaded && s.action == action_state::closed && !manipulating;
	}
	inline void feed(state& s) noexcept
	{
		s.action = action_state::closed;
		if (!s.chamber_loaded && s.magazine_inserted && s.magazine_rounds > 0)
		{
			--s.magazine_rounds;
			s.chamber_loaded = true;
		}
	}
	inline void finish_cycle(const rules& r, state& s) noexcept
	{
		feed(s);
		if (r.last_round_lock && s.magazine_inserted && !s.chamber_loaded)
			s.action = action_state::locked_open;
	}
	inline bool release(const rules& r, state& s) noexcept
	{
		if (!valid(r, s) || s.action != action_state::locked_open ||
			(s.magazine_inserted && s.magazine_rounds == 0)) return false;
		// An inserted empty follower retains the lock; no magazine permits
		// closure but cannot create a chambered round.
		feed(s);
		return true;
	}
	inline bool fire(const rules& r, state& s) noexcept
	{
		if (!ready(r, s)) return false;
		s.chamber_loaded = false;
		finish_cycle(r, s);
		return true;
	}
	// Returns a live extracted round to the caller's chosen inventory policy.
	// A partial pull never calls this function; only a complete stroke/release.
	inline bool cycle(const rules& r, state& s, int& extracted) noexcept
	{
		if (!valid(r, s) || s.action == action_state::held_open || s.action == action_state::latched_open) return false;
		extracted = int(s.chamber_loaded);
		s.chamber_loaded = false;
		finish_cycle(r, s);
		return true;
	}
	inline bool extract(const rules& r, state& s, int& extracted) noexcept
	{
		if (!valid(r, s) || s.action == action_state::held_open || s.action == action_state::latched_open) return false;
		extracted = int(s.chamber_loaded);
		s.chamber_loaded = false;
		s.action = action_state::held_open;
		return true;
	}
	inline bool finish_stroke(const rules& r, state& s) noexcept
	{
		if (!valid(r, s) || s.action != action_state::held_open) return false;
		finish_cycle(r, s);
		return true;
	}
	// The receiver catch is distinct from an empty follower and from a hand
	// holding the action back. Toggling it never performs extraction or feeding.
	inline bool latch(const rules& r,state& s) noexcept
	{
		if (!valid(r,s) || !r.manual_catch || s.action!=action_state::held_open) return false;
		s.action=action_state::latched_open;
		return true;
	}
	inline bool unlatch(const rules& r,state& s) noexcept
	{
		if (!valid(r,s) || !r.manual_catch || s.action!=action_state::latched_open) return false;
		s.action=action_state::held_open;
		return true;
	}
	inline bool slap_release(const rules& r,state& s) noexcept
	{
		if (!unlatch(r,s)) return false;
		finish_cycle(r,s);
		return true;
	}

	// Temporary native-auto-reload bridge: the engine still seats and cycles
	// the magazine. A ready loaded total N partitions to (N-1)+1, never N+1.
	// Do not use this inference once physical magazine/slide control is enabled.
	inline bool from_native_automatic(const rules& r, int loaded, state& output) noexcept
	{
		if (loaded < 0 || r.magazine_capacity <= 0 || r.magazine_capacity > 1000 ||
			loaded > r.magazine_capacity + int(r.plus_one)) return false;
		state next{true, loaded > 0, loaded > 0 ? loaded - 1 : 0,
			loaded == 0 && r.last_round_lock ? action_state::locked_open : action_state::closed};
		if (!valid(r, next)) return false;
		output = next;
		return true;
	}
	inline int reload_capacity(const rules& r, bool retained_chamber) noexcept
	{
		if (r.magazine_capacity <= 0 || r.magazine_capacity > 1000) return 0;
		return r.magazine_capacity + int(r.plus_one && retained_chamber);
	}
}
