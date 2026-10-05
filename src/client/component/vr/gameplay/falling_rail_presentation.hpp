#pragma once
#include "falling_trajectory.hpp"

namespace vr::gameplay::motion
{
	// Visual rail ownership belongs to the exact native skin record. A later
	// simulation publication must not move an already displayed departure.
	class rail_presentation
	{
		flight displayed_{};
		clock::time_point last_at_{};
		bool ready_{},followed_{};
	public:
		void reset() noexcept {*this={};}
		bool needs_parent(const flight& published,clock::time_point at) const noexcept
		{
			const auto deadline=published.born+std::chrono::duration_cast<clock::duration>(std::chrono::duration<float>(published.rail_seconds));
			return published.rail_seconds>0 && (at<deadline || !ready_ || displayed_.born!=published.born || !displayed_.detached);
		}
		anchor pose(const flight& published,clock::time_point at,const anchor* attached) noexcept
		{
			if(published.rail_seconds<=0)return published.pose(at);
			if(!ready_ || displayed_.born!=published.born)
			{displayed_=published;last_at_=published.born;ready_=true;followed_=false;}
			const auto deadline=published.born+std::chrono::duration_cast<clock::duration>(std::chrono::duration<float>(published.rail_seconds));
			if(at<deadline)
			{
				auto current=published;
				if(attached){auto candidate=published;candidate.start=*attached;if(candidate.valid())current.start=*attached;else attached=nullptr;}
				else if(followed_)current.start=displayed_.start;
				// An older queued job can use its own native attachment, but cannot
				// rewind the departure retained for newer scene jobs.
				if(!displayed_.detached && at>=last_at_)
				{displayed_.start=current.start;last_at_=at;followed_|=attached!=nullptr;}
				return current.pose(at);
			}
			if(!displayed_.detached)
			{
				if(!followed_)displayed_=published;
				if(!displayed_.detached)
				{
					auto departure=displayed_.start;
					if(followed_ && attached && last_at_<deadline && at>last_at_)
					{
						auto candidate=published;candidate.start=*attached;
						if(candidate.valid())
						{
							const float t=std::clamp(std::chrono::duration<float>(deadline-last_at_).count()/
								std::chrono::duration<float>(at-last_at_).count(),0.f,1.f);
							departure.position=hands::add(departure.position,hands::scale(hands::sub(attached->position,departure.position),t));
							departure.rotation=hands::blend_quat(departure.rotation,attached->rotation,t);
						}
					}
					displayed_.start=departure;displayed_.exit=translate_local(departure,displayed_.rail);
					displayed_.velocity=hands::scale(hands::rotate(departure.rotation,displayed_.rail),1/displayed_.rail_seconds);
					displayed_.detached=true;
				}
			}
			return displayed_.pose(at);
		}
	};
}
