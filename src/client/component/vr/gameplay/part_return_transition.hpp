#pragma once
#include "physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	// Cosmetic only. The action has already closed/locked mechanically; this
	// finite transition must never delay feeding, input or extraction authority.
	class part_return_transition
	{
	public:
		bool active() const noexcept { return returning_; }
		float update(std::uint64_t instance, std::uint64_t reference, bool held,
			float target, clock::time_point now, float seconds) noexcept
		{
			if (instance != instance_ || reference != reference_ || now < last_at_ ||
				(now-last_at_) > std::chrono::milliseconds(150))
			{ returning_ = was_held_ = false; value_ = target; target_ = target; }
			// Advance the OLD segment to this render time before replacing its
			// target. Otherwise a target changing on every frame keeps restarting
			// at t=0 with the same stale value, freezing motion until updates stop.
			if(returning_ && seconds>0)
			{
				const auto t=std::clamp(std::chrono::duration<float>(now-released_).count()/seconds,0.f,1.f);
				value_=target_+(start_-target_)*(1-t)*(1-t);
				if(t>=1)returning_=false;
			}
			if (!held && (was_held_ || target < target_-.00001f))
			{ returning_ = true; start_ = value_; released_ = now; }
			if (held) returning_ = false;
			value_ = target;
			if (returning_ && seconds > 0)
			{
				const float t = std::clamp(std::chrono::duration<float>(now-released_).count()/seconds,0.f,1.f);
				const float remaining = (1-t)*(1-t);
				value_ = target + (start_-target)*remaining;
				if (t >= 1) returning_ = false;
			}
			instance_ = instance; reference_ = reference; was_held_ = held; last_at_ = now; target_ = target;
			return value_;
		}
	private:
		std::uint64_t instance_{}, reference_{};
		bool was_held_{}, returning_{};
		float start_{}, value_{}, target_{};
		clock::time_point released_{}, last_at_{};
	};
}
