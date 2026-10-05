#pragma once
#include "physical_reload_geometry.hpp"
#include <chrono>

namespace vr::gameplay::weapons::physical_reload
{
	struct impact_profile
	{
		float radius, rearm_radius, min_speed, min_travel;
		vec direction;
		float direction_cosine{}; // Zero preserves the original axial-speed rule.
		bool follow_approach_peak{}; // Articulated hand wind-up can begin below the target.
	};
	struct impact_observation
	{
		float dt{}, max_step{}, step_limit{}, separation{}, distance{}, speed{}, direction_cosine{}, travel{};
		int point{-1}, jump_point{-1};
		vec previous{}, current{}, start{};
		bool sampled{}, continuous{}, separated{}, armed_before{}, armed_after{}, entered{}, hit{};
		bool speed_ok{}, direction_ok{}, travel_ok{}, above{}, radius_ok{};
	};
	// Fixed-size swept skin contacts for bare-hand handle slaps. Solid magazine
	// endcaps use swept_box_contact. Failed transactions still consume approaches.
	template<size_t N=3> class swept_impact
	{
	public:
		using clock=std::chrono::steady_clock;
		void reset() noexcept { *this={}; }
		bool update(const impact_profile& p,const std::array<vec,N>& points,clock::time_point at,float max_step,
			impact_observation* observed=nullptr) noexcept
		{
			const float dt=std::chrono::duration<float>(at-at_).count();
			if (observed) { *observed={}; observed->dt=sampled_ ? dt : 0; observed->step_limit=max_step; observed->sampled=sampled_; observed->separation=1e9f; observed->distance=1e9f; }
			bool continuous=sampled_ && dt>0 && dt<=.15f;
			bool separated=true;
			for (size_t i=0;i<points.size();++i)
			{
				if (observed)
				{
					const float step=hands::length(hands::sub(points[i],previous_[i]));
					if (step>observed->max_step) { observed->max_step=step; observed->jump_point=sampled_ && step>max_step ? int(i) : -1; }
					observed->separation=std::min(observed->separation,hands::length(points[i]));
				}
				continuous=continuous && hands::length(hands::sub(points[i],previous_[i]))<=max_step;
				separated=separated && hands::length(points[i])>p.rearm_radius;
			}
			if (!continuous) armed_=false;
			// Rearming grants one approach, but is not necessarily its high point.
			// A hand can start below the handle, rise, then slap down. Keep the
			// furthest point opposite the accepted direction, without accumulating
			// back-and-forth jitter or granting another attempt after a hit.
			if (continuous && armed_ && p.follow_approach_peak)
				for (size_t i=0;i<points.size();++i)
					if (hands::dot(hands::sub(points[i],start_[i]),p.direction)<0) start_[i]=points[i];
			if (observed)
			{
				observed->continuous=continuous; observed->separated=separated; observed->armed_before=armed_;
				for (size_t i=0;i<points.size();++i)
				{
					const auto delta=hands::sub(points[i],previous_[i]); const float length=hands::length(delta);
					const float distance=sampled_ ? segment_distance({},previous_[i],points[i]) : hands::length(points[i]);
					observed->entered|=sampled_ && distance<=p.radius && hands::length(previous_[i])>p.radius;
					if (distance>=observed->distance) continue;
					observed->distance=distance; observed->point=int(i);
					observed->previous=previous_[i]; observed->current=points[i]; observed->start=start_[i];
					observed->speed=dt>0 && sampled_ ? length/dt : 0;
					observed->direction_cosine=length>1e-6f ? hands::dot(delta,p.direction)/length : 0;
					observed->travel=hands::dot(hands::sub(points[i],start_[i]),p.direction);
					observed->speed_ok=length>=p.min_speed*dt && dt>0;
					observed->direction_ok=observed->direction_cosine>=p.direction_cosine;
					observed->travel_ok=observed->travel>=p.min_travel;
					observed->above=hands::dot(previous_[i],p.direction)<0;
					observed->radius_ok=distance<=p.radius;
				}
			}
			bool hit=false;
			if (continuous && armed_)
				for (size_t i=0;i<points.size();++i)
				{
					const auto delta=hands::sub(points[i],previous_[i]);
					const auto distance=hands::length(delta), directed=hands::dot(delta,p.direction);
					const bool direction=p.direction_cosine>0 ? distance>=p.min_speed*dt && directed>=distance*p.direction_cosine : directed>=p.min_speed*dt;
					if (direction &&
						hands::dot(hands::sub(points[i],start_[i]),p.direction)>=p.min_travel &&
						hands::dot(previous_[i],p.direction)<0 &&
						segment_distance({},previous_[i],points[i])<=p.radius)
					{
						hit=true;
						// Report the actual winning contact, not a closer rejected finger.
						if (observed)
						{
							observed->point=int(i); observed->previous=previous_[i]; observed->current=points[i]; observed->start=start_[i];
							observed->distance=segment_distance({},previous_[i],points[i]); observed->speed=distance/dt;
							observed->direction_cosine=distance>1e-6f ? directed/distance : 0;
							observed->travel=hands::dot(hands::sub(points[i],start_[i]),p.direction);
							observed->speed_ok=observed->direction_ok=observed->travel_ok=observed->above=observed->radius_ok=true;
						}
					}
				}
			if (hit) armed_=false;
			else if (separated && !armed_) { armed_=true; start_=points; }
			if (observed) { observed->hit=hit; observed->armed_after=armed_; }
			previous_=points; at_=at; sampled_=true;
			return hit;
		}
	private:
		std::array<vec,N> previous_{},start_{};
		clock::time_point at_{};
		bool sampled_{},armed_{};
	};
}
