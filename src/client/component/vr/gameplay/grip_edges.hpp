#pragma once
#include "../controller_input.hpp"
#include "weapon_holding.hpp"

namespace vr::gameplay::weapons::carry
{
	struct grip_edges { unsigned pressed{}, released{}, deferred{}, resumed{}; };
	// Body-return equipment needs no world placement pose. An active, fresh
	// neutral button can settle its held relation even if an edge was lost.
	inline unsigned neutral_grips(const controller_input::frame& input,controller_input::clock::time_point now)noexcept
	{
		if(!input.focused || !input.sequence || now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150))return 0;
		unsigned result{};for(unsigned h=0;h<2;++h)if(input.squeeze[h].active && !input.squeeze[h].down)result|=1u<<h;return result;
	}
	class grip_edge_gate
	{
	public:
		grip_edges consume(const controller_input::frame& input,bool gameplay,controller_input::clock::time_point now) noexcept
		{
			grip_edges out;
			const bool fresh=gameplay && input.focused && input.sequence && now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150);
			for (std::size_t h=0;h<2;++h)
			{
				auto& s=hands_[h];const auto& button=input.squeeze[h];
				const bool valid=fresh && button.active;
				const bool pose=input.grip[h].valid;
				if (!valid || !s.seen || button.generation!=s.button.generation || input.reference_generation!=s.reference ||
					button.presses<s.button.presses || button.releases<s.button.releases ||
					input.sequence<s.sequence || now<s.at)
				{
					// Do not acquire on resume. A held button can still authorize its
					// NEXT real release, even if its original press was not observed.
					s={button,input.reference_generation,input.sequence,now,valid,valid && button.down,false};
					if (s.armed) out.resumed|=1u<<h;
					continue;
				}
				if (button.presses>s.button.presses)
				{
					s.pending=false;s.armed=true;
					if (pose) out.pressed|=1u<<h;
				}
				if (button.releases>s.button.releases && s.armed)
				{s.pending=true;s.armed=false;}
				// Button counters survive a missing pose. Retain a proven release
				// until this hand has geometry; pose loss alone never releases it.
				if (s.pending)
				{
					if (pose) {out.released|=1u<<h;s.pending=false;}
					else out.deferred|=1u<<h;
				}
				s.button=button;s.reference=input.reference_generation;s.sequence=input.sequence;s.at=now;
			}
			return out;
		}
		// A delayed pickup's currently held grip must retain
		// the next physical release rather than require a second squeeze.
		void adopt(hand h,const controller_input::frame& input,controller_input::clock::time_point now)noexcept
		{
			if(!valid_hand(h))return;const auto& button=input.squeeze[static_cast<unsigned>(h)];
			const bool valid=input.focused && button.active && input.sequence && now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150);
			hands_[static_cast<unsigned>(h)]={button,input.reference_generation,input.sequence,now,valid,valid && button.down,false};
		}
		// An exchanged weapon remains held after the release which acquired it.
		void latch(hand h) noexcept
		{
			if (valid_hand(h)) {auto& s=hands_[static_cast<int>(h)];s.armed=s.pending=false;}
		}
	private:
		struct state {controller_input::digital_action button{};std::uint64_t reference{},sequence{};controller_input::clock::time_point at{};bool seen{},armed{},pending{};};
		std::array<state,2> hands_{};
	};
}
