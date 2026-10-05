#pragma once
#include <algorithm>
#include <cmath>

namespace vr::gameplay::weapons::manual_bolt
{
	// Instance-owned mechanics. Lift and travel are normalized mechanical coordinates,
	// not animation times. The common magazine owner remains the sole live-round ledger.
	struct state
	{
		float lift{}, travel{};
		bool spent_case{}, cocked{true}, feeding{}, feed_armed{};
		bool returned{}; // A real forward stroke reached the receiver this cycle.
	};
	struct target
	{
		float lift{}, travel{};
	};
	enum class event
	{
		none,
		unlock,
		lock,
		eject_case,
		eject_live,
		feed,
		close
	};
	inline bool locked(const state& s) noexcept
	{
		return s.lift == 0 && s.travel == 0;
	}
	inline bool valid(const state& s, bool live) noexcept
	{
		return std::isfinite(s.lift) && std::isfinite(s.travel) && s.lift >= 0 && s.lift <= 1 && s.travel >= 0 &&
		       s.travel <= 1 && (s.travel == 0 || s.lift == 1) && !(s.spent_case && (live || s.feeding)) &&
		       !(live && s.feeding) && (!s.feeding || (s.travel > 0 && s.travel < .85f)) &&
		       (!s.feed_armed || (!live && !s.spent_case && !s.feeding));
	}
	inline bool ready(const state& s, bool live) noexcept
	{
		return valid(s, live) && locked(s) && s.cocked && live;
	}
	inline bool fire(state& s, bool& live) noexcept
	{
		if (!ready(s, live))
			return false;
		live = false;
		s.spent_case = true;
		s.cocked = false;
		return true;
	}
	// Only a constrained, current controller sample may request motion. Mechanical
	// validation also rejects shortcuts, so render prediction has no feed authority.
	inline bool move(state& s, bool& live, int& magazine, bool inserted, target t, event& feedback, int& spent) noexcept
	{
		if (!valid(s, live) || !std::isfinite(t.lift) || !std::isfinite(t.travel) || t.lift < 0 || t.lift > 1 ||
		    t.travel < 0 || t.travel > 1 || (t.travel > 0 && t.lift != 1) || (s.travel > 0 && t.lift != 1) ||
		    (s.lift != 1 && t.travel != s.travel))
			return false;
		if (s.lift == t.lift && s.travel == t.travel)
			return false;
		feedback = event::none;
		spent = 0;
		if (s.lift < 1 && t.lift == 1)
		{
			s.cocked = true;
			feedback = event::unlock;
		}
		if (t.travel >= .85f && s.travel < .85f)
		{
			if (live || s.feeding)
			{
				spent = 1;
				feedback = event::eject_live;
			}
			else if (s.spent_case)
				feedback = event::eject_case;
			live = s.feeding = s.spent_case = false;
		}
		if (t.travel >= .95f)
			s.feed_armed = true;
		if (t.travel <= .35f && s.travel > .35f && s.feed_armed)
		{
			s.feed_armed = false;
			if (inserted && magazine > 0)
			{
				--magazine;
				s.feeding = true;
				feedback = event::feed;
			}
		}
		if (t.travel == 0 && s.travel > 0)
		{
			s.returned = true;
			if (s.feeding)
			{
				s.feeding = false;
				live = true;
			}
			feedback = event::close;
		}
		if (t.lift == 0 && s.lift > 0)
			feedback = event::lock;
		if (t.travel > 0 || (s.lift == 0 && t.lift > 0)) s.returned = false;
		s.lift = t.lift;
		s.travel = t.travel;
		return valid(s, live);
	}
} // namespace vr::gameplay::weapons::manual_bolt
