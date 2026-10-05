#pragma once
#include "controller_firing.hpp"

namespace vr::gameplay::weapons::independent_fire
{
	struct timing {int mode{},interval{},delay{},burst_pause{};bool mechanical_cycle{};};
	// H2 Ranger's witnessed native descriptor uses mode 6 and a 10 ms interval.
	// It remains edge-fired (one barrel per press), never automatic or a burst.
	inline constexpr int double_barrel_mode=6;
	inline bool valid(timing t) noexcept
	{return t.mechanical_cycle ? t.mode==1 && t.interval==0 && t.delay==0 && t.burst_pause==0 : t.mode>=0 && t.mode<=double_barrel_mode && t.interval>=(t.mode==double_barrel_mode ? 10 : 20) && t.interval<=10000 && t.delay>=0 && t.delay<=10000 && t.burst_pause>=0 && t.burst_pause<=10000;}
	inline constexpr timing mechanical_cycle_timing()noexcept{return {1,0,0,0,true};}

	inline bool decode_native_timing(int mode,int interval,int delay,float burst_pause_milliseconds,timing& out) noexcept
	{
		timing candidate{mode,interval,delay,0};
		if (mode>=2 && mode<=4)
		{
			// H2 stores a float, but PM_Weapon consumes it directly as milliseconds.
			// Do not convert seconds here: native M16/FAMAS/M93R all return 200.
			if (!std::isfinite(burst_pause_milliseconds) || burst_pause_milliseconds<0 || burst_pause_milliseconds>10000) return false;
			candidate.burst_pause=int(std::floor(burst_pause_milliseconds+.5f));
		}
		if (!valid(candidate)) return false;
		out=candidate;return true;
	}
	// Per-instance simulation clock, never indexed by physical hand. No backlog
	// firing after a stall. Losing authority cancels intent, not the shot cooldown.
	class shot_clock
	{
	public:
		bool due(const controller_input::frame& input,const hold& owner,bool enabled,bool pose_ready,
			controller_input::clock::time_point now,int command,timing t) noexcept
		{
			if (!valid(t) || command<0) {cancel();return false;}
			if (command<last_command_) {cancel();next_=0;}
			if (last_command_ && command-last_command_>150) cancel();
			last_command_=command;
			if (owner.id()!=owner_.id() || owner.rear_revision!=owner_.rear_revision || owner.rear!=owner_.rear ||
				reference_!=input.reference_generation) cancel();
			owner_=owner;reference_=input.reference_generation;
			const bool down=trigger_.consume(input,owner,enabled,pose_ready,now);
			if (!enabled || !pose_ready) {pending_=false;burst_=0;was_down_=false;return false;}
			const bool pressed=down && !was_down_;was_down_=down;
			if (!down && (t.mode<2 || t.mode>4)) pending_=false;
			const bool rate_ready=t.mechanical_cycle || command>=next_;
			if (!pending_ && rate_ready && (t.mode==0 ? down : pressed))
			{
				burst_=t.mode>=2 && t.mode<=4 ? t.mode : 1;
				pending_=true;due_=std::int64_t(command)+t.delay;
			}
			return pending_ && command>=due_ && rate_ready && command!=attempted_;
		}
		// Admission failed before any native shot. Consume this attempt/edge,
		// but do not manufacture or clear a successful-shot cooldown.
		void rejected(int command)noexcept
		{attempted_=command;pending_=false;burst_=0;}

		void settled(int command,timing t,bool emitted) noexcept
		{
			attempted_=command;
			next_=std::int64_t(command)+t.interval;
			if (!emitted || --burst_<=0)
			{pending_=false;burst_=0;if (emitted && t.mode>=2 && t.mode<=4) next_+=t.burst_pause;}
			else due_=next_;
		}
		void cancel() noexcept {trigger_.reset();pending_=false;burst_=0;was_down_=false;}
	private:
		trigger_policy trigger_{};hold owner_{};
		std::uint64_t reference_{};
		std::int64_t next_{},due_{};int last_command_{},attempted_{-1},burst_{};
		bool pending_{},was_down_{};
	};
}
