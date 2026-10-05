#pragma once
#include "physical_reload_geometry.hpp"
#include "swept_box_contact.hpp"
#include <optional>
#include <chrono>

namespace vr::gameplay::weapons::physical_reload
{
	// Opt-in physical latch policy, in metres and gun-local directions. Authored
	// mesh contacts live in reload_profile; no weapon names enter this controller.
	struct magazine_manipulation
	{
		float grab_radius, pull_travel, pull_lateral_limit;
		vec pull_axis;
		float latch_radius, latch_rearm_radius, latch_min_speed, latch_min_travel;
		vec latch_direction;
		bool spare_strike{true};
		// Resolve overlapping contacts using the tracked wrist's facing relative
		// to the two authored grasps. Different finger points/box sizes do not
		// provide comparable distances. Outside overlaps, contact alone suffices.
		bool prefer_grasp_facing{};
	};
	// Shared AK-style spare-magazine latch strike. Keep intentional motion and
	// rearm separation while allowing a four-centimetre contact radius.
	inline constexpr magazine_manipulation rocking_magazine(vec pull_axis={0,0,-1}) noexcept
	{return {.05f,.05f,.10f,pull_axis,.04f,.075f,.15f,.012f,{1,0,0},true};}

	inline bool valid(const magazine_manipulation& p) noexcept
	{
		for (float v : {p.grab_radius,p.pull_travel,p.pull_lateral_limit,p.latch_radius,
			p.latch_rearm_radius,p.latch_min_speed,p.latch_min_travel})
			if (!std::isfinite(v) || v<=0 || v>1) return false;
		for (auto axis : {p.pull_axis,p.latch_direction})
		{
			for (float v:axis) if (!std::isfinite(v)) return false;
			if (std::abs(hands::dot(axis,axis)-1)>.001f) return false;
		}
		return p.latch_rearm_radius>p.latch_radius && p.latch_min_travel<p.latch_rearm_radius;
	}
	struct magazine_contact
	{
		bool valid{};
		float grip_distance{};
		// Raw rigid magazine pose relative to the gun-local latch, in metres.
		// Absent for magazines without a strike policy.
		std::optional<box_motion> strike;
		struct directed_strike {box_motion motion;vec direction;};
		std::optional<directed_strike> second_strike;
	};
	inline bool valid(const magazine_contact& c) noexcept
	{
		if (!c.valid || !std::isfinite(c.grip_distance) || c.grip_distance<0) return false;
		if(c.strike && !valid(*c.strike))return false;
		if(c.second_strike)
		{
			if(!c.strike || !valid(c.second_strike->motion))return false;
			for(float v:c.second_strike->direction)if(!std::isfinite(v))return false;
			if(std::abs(hands::dot(c.second_strike->direction,c.second_strike->direction)-1)>.001f)return false;
		}
		return true;
	}
	// A stroke is consumed on contact even if the native compare fails. Only a
	// fresh separated approach can try again. This also handles spawn-in-contact,
	// tracking jumps, very slow overlaps and frame-rate-independent swept impacts.
	class magazine_latch_contact
	{
	public:
		using clock=std::chrono::steady_clock;
		void reset() noexcept { *this={}; }
		bool update(const magazine_manipulation& p,const box_motion& current,
			clock::time_point at,float max_step) noexcept
		{
			if(!valid(p) || !valid(current) || !std::isfinite(max_step) || max_step<=0){reset();return false;}
			const float dt=std::chrono::duration<float>(at-at_).count();
			const box_sweep sweep(previous_,current);
			const bool continuous=sampled_ && dt>0 && dt<=.15f && same_regions(previous_,current) && sweep.bound<=max_step;
			bool separated=true;
			for(size_t i=0;i<current.region_count;++i)separated=separated && closest_box(current,i).distance>p.latch_rearm_radius;
			if(!continuous)armed_=false;
			bool entered=false,hit=false;
			if(continuous && armed_)for(size_t i=0;i<current.region_count;++i)
			{
				vec local{};if(!sweep.contact(i,p.latch_radius,local))continue;
				entered=true;
				// Follow the same material point through all poses; changing closest
				// corners cannot manufacture speed or accumulated approach travel.
				const auto before=box_point(previous_.frame,local),now=box_point(current.frame,local),start=box_point(start_,local);
				hit|=hands::dot(hands::sub(now,before),p.latch_direction)>=p.latch_min_speed*dt &&
					hands::dot(hands::sub(now,start),p.latch_direction)>=p.latch_min_travel && hands::dot(before,p.latch_direction)<0;
			}
			if(entered)armed_=false;
			else if(separated && !armed_){armed_=true;start_=current.frame;}
			previous_=current;at_=at;sampled_=true;return hit;
		}
	private:
		box_motion previous_{};hands::anchor start_{};
		clock::time_point at_{};bool sampled_{},armed_{};
	};
	// Independent approach histories per physical release, one transaction per
	// sample. Contact at one latch cannot arm or lend velocity to the other.
	class magazine_latch_contacts
	{
		magazine_latch_contact first_,second_;
	public:
		void reset() noexcept {first_.reset();second_.reset();}
		bool update(const magazine_manipulation& p,const magazine_contact& c,
			magazine_latch_contact::clock::time_point at,float max_step) noexcept
		{
			if(!valid(c) || !c.strike){reset();return false;}
			bool hit=first_.update(p,*c.strike,at,max_step);
			if(c.second_strike)
			{auto policy=p;policy.latch_direction=c.second_strike->direction;hit=second_.update(policy,c.second_strike->motion,at,max_step) || hit;}
			else second_.reset();
			if(hit)reset();
			return hit;
		}
	};
}
