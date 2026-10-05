#pragma once
#include "part_return_transition.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	// A release finishes on the acquired side, even if the owner changes hands
	// before another grasp. Identity/reference discontinuities discard the pose.
	class charging_handle_fold_motion
	{
		part_return_transition transition_;
		std::uint64_t instance_{},reference_{},ownership_{};int hand_{};bool held_{};
	public:
		int hand() const noexcept{return hand_;}
		float update(std::uint64_t instance,std::uint64_t reference,bool held,int hand,
			clock::time_point now,float seconds,std::uint64_t ownership=0) noexcept
		{
			if(instance!=instance_ || reference!=reference_ || ownership!=ownership_)
			{held_=false;hand_=hand==1?1:0;transition_={};}
			if(held && !held_)hand_=hand==1?1:0;
			held_=held;instance_=instance;reference_=reference;ownership_=ownership;
			return transition_.update(instance,reference,held,held?1.f:0.f,now,seconds);
		}
	};
}
