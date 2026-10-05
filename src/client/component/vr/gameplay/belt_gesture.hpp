#pragma once
#include "belt_profile.hpp"
#include "belt_feed.hpp"
#include "belt_cover_push.hpp"
#include "hinge_travel.hpp"

namespace vr::gameplay::weapons::belt_feed
{
	inline bool finite_contact(const contact& c)noexcept
	{return finite_part_vec(c.hand) && finite_part_vec(c.chain_point) && std::isfinite(c.cover_angle) &&
		std::isfinite(c.cover_distance) && c.cover_distance>=0 && std::isfinite(c.chain_distance) && c.chain_distance>=0 &&
		std::isfinite(c.feed_distance) && c.feed_distance>=0 && std::isfinite(c.alignment) && std::abs(c.alignment)<=1.001f &&
		std::isfinite(c.bridge_distance) && c.bridge_distance>=0 && std::isfinite(c.bridge_angle) &&
		std::isfinite(c.release_distance) && c.release_distance>=0;}
	inline bool release_available(const profile& p,state s,const contact& c)noexcept
	{return p.bridge && p.bridge->release && s.bridge==0 && s.cover==0 && c.release_distance<=p.bridge->release_radius;}
	inline bool bridge_available(const profile& p,state s,const contact& c)noexcept
	{return p.bridge && c.bridge_settled && (!p.bridge->release || s.bridge>0) && c.bridge_distance<=p.bridge->radius;}
	inline float cover_target(const profile& p,const grasp& g,const contact& c)noexcept
	{
		const float delta=std::remainder(c.cover_angle-g.angle,6.283185307f);
		return std::clamp(g.initial+delta/p.cover_angle,0.f,1.f);
	}
	inline void advance_hinge(grasp& g,float angle,float stroke)noexcept
	{
		advance_hinge_travel(g.initial,g.angle,angle,stroke);
	}
	inline bool preferred(const profile& p,state s,bool box,int rounds,const contact& c,float handle_distance,float box_distance)noexcept
	{
		const bool chain=accessible(s) && box && rounds>0 && !s.laid && c.chain_distance<=p.chain_radius;
		const auto other=std::min(handle_distance,box && accessible(s) ? box_distance : 10.f);
		return (chain && c.chain_distance<=other+p.chain_preference) ||
			(cover_available(s,p.bridge!=nullptr) && c.bridge_settled && c.cover_distance<=p.cover_radius && c.cover_distance<=other+p.cover_preference) ||
			(bridge_available(p,s,c) && c.bridge_distance<=other+p.cover_preference) ||
			(release_available(p,s,c) && c.release_distance<=other+p.cover_preference);
	}
	class controller
	{
	public:
		grasp current()const noexcept{return grip_;}
		const char* push_reason()const noexcept{return push_.reason();}
		bool busy()const noexcept{return grip_.part!=lease::none;}
		void reset()noexcept{grip_={};push_.reset();}
		template<class Move,class Lay,class Bridge>bool update(const profile& p,state s,bool box,int rounds,const contact& c,
			bool pressed,bool down,bool available,Move&& move,Lay&& lay,Bridge&& bridge,
			controller_input::clock::time_point now={},bool open_palm=false)
		{
			if(busy())
			{
				// Include the release frame: time spent holding an already open
				// cover cannot consume the post-release cooldown.
				if(grip_.part==lease::cover)push_.cooldown(now);else push_.cancel();
				if(!available || !down || hands::length(hands::sub(c.hand,grip_.previous))>.25f ||
					(grip_.part==lease::bridge_release ? c.release_distance>.3f : grip_.part==lease::bridge ? !p.bridge || c.bridge_distance>.4f : grip_.part==lease::cover ? c.cover_distance>.35f : !box || !accessible(s) || c.chain_distance>.45f))
				{grip_={};return true;}
				grip_.previous=c.hand;
				if(grip_.part==lease::bridge_release)return true; // One press; never become a return grasp while held.
				if(grip_.part==lease::bridge)
				{
					advance_hinge(grip_,c.bridge_angle,p.bridge->angle);
					const auto wanted=std::clamp(grip_.initial,0.f,1.f);
					const auto target=settled_cover(s.cover>0 ? std::max(wanted,bridge_clearance):wanted);
					if(std::abs(target-s.bridge)>.001f && !bridge(target))grip_={};
				}
				else if(grip_.part==lease::cover)
				{
					advance_hinge(grip_,c.cover_angle,p.cover_angle);
					const auto target=settled_cover(cover_target(p,grip_,c));
					if(std::abs(target-s.cover)>.001f && !move(target))grip_={};
				}
				else if(c.feed_distance<=p.lay_radius && c.alignment>=p.lay_cosine &&
					(grip_.initial<.04f || grip_.initial-c.feed_distance>=.02f))
				{(void)lay();grip_={};} // A rejected compare consumes this grip, too.
				return true;
			}
			// A trigger grasp anywhere (including a different part's winning
			// candidate) overrides contact motion. Do not swallow that edge.
			if(down)push_.cancel();
			if(!available || !pressed || !down)
				return push_.update(p,s,c.push,now,available && open_palm && c.bridge_settled,move);
			push_.cancel(); // An explicit part grasp always wins over a push.
			const bool chain=accessible(s) && box && rounds>0 && !s.laid && c.chain_distance<=p.chain_radius;
			if(release_available(p,s,c))
			{
				(void)bridge(1.f); // Automatic fall is visual; failure consumes this press too.
				grip_={lease::bridge_release,0,0,c.hand};return true;
			}
			const bool cover=cover_available(s,p.bridge!=nullptr) && c.bridge_settled && c.cover_distance<=p.cover_radius;
			const bool rail=bridge_available(p,s,c);
			if(chain && (!cover || c.chain_distance<=c.cover_distance) && (!rail || c.chain_distance<=c.bridge_distance))grip_={lease::chain,c.feed_distance,0,c.hand};
			else if(rail && (!cover || c.bridge_distance<c.cover_distance))grip_={lease::bridge,s.bridge,c.bridge_angle,c.hand};
			else if(cover)grip_={lease::cover,s.cover,c.cover_angle,c.hand};
			else if(chain)grip_={lease::chain,c.feed_distance,0,c.hand};
			return busy();
		}
	private:grasp grip_{};cover_push push_{};
	};
}
