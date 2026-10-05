#pragma once
#include "belt_profile.hpp"
#include "belt_feed.hpp"
#include "../controller_input.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"

namespace vr::gameplay::weapons::belt_feed
{
	inline constexpr float push_contact_gap=.012f,push_rearm_gap=.025f,push_penetration=.025f,push_outer_gap=.12f;
	inline constexpr float push_palm_radius=.06f,push_max_speed=6.f;
	inline constexpr float push_long_extension=.015f; // Toward fingers AND wrist; width is unchanged.
	inline hands::vec push_extension(hands::vec along)noexcept
	{
		const auto n=hands::length(along);
		return std::isfinite(n) && n>1e-6f?hands::vec{along[0]*push_long_extension/n,along[1]*push_long_extension/n,0}:hands::vec{};
	}
	inline bool push_inside(const cover_push_profile& s,hands::vec v,hands::vec along={})noexcept
	{
		using namespace hands;
		// Capsule/rectangle distance in the cover plane. This extends the
		// accepted palm only along the actual wrist-to-fingers direction.
		v[2]=0;const auto d=push_extension(along),a=sub(v,d),b=add(v,d);
		const vec low{-s.outer,s.side_low,0},high{-s.inner,s.side_high,0};
		float distance=std::min(physical_reload::box_distance(a,low,high),physical_reload::box_distance(b,low,high));
		// A crossing segment has distance zero, even if neither endpoint is in the rectangle.
		float enter=0,leave=1;bool crossing=true;
		for(int i=0;i<2;++i)
		{
			const auto delta=b[i]-a[i];
			if(std::abs(delta)<1e-8f){if(a[i]<low[i] || a[i]>high[i])crossing=false;continue;}
			auto t0=(low[i]-a[i])/delta,t1=(high[i]-a[i])/delta;if(t0>t1)std::swap(t0,t1);
			enter=std::max(enter,t0);leave=std::min(leave,t1);
		}
		if(crossing && enter<=leave)return true;
		for(float x:{low[0],high[0]})for(float y:{low[1],high[1]})
			distance=std::min(distance,physical_reload::segment_distance({x,y,0},a,b));
		return distance<=push_palm_radius;
	}
	struct push_geometry {hands::vec point{},normal{};float gap{};bool inside{},facing{};hands::vec along{};};
	inline push_geometry query_push(const profile& p,float cover,const cover_push_contact& c)noexcept
	{
		using namespace hands;
		if(!p.push)return {};
		const auto rotation=conjugate(hinge_pose({},p.cover_axis,p.cover_angle,cover).rotation);
		const auto v=rotate(rotation,c.point),n=rotate(rotation,c.palm),along=rotate(rotation,c.along);const auto& s=*p.push;
		return {v,n,v[2]-s.surface,push_inside(s,v,along),
			n[2]<.2f && std::abs(dot(c.palm,c.palm)-1)<.02f,along};
	}
	inline cover_push_contact sample_cover_push(const profile& p,hands::anchor gun,hands::vec point,
		hands::vec palm,hands::vec world,float units,hands::vec along_world={}) noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		if(!p.push || !std::isfinite(units) || units<=0)return {};
		const auto frame=inverse(compose(gun,p.cover_rest));
		const auto n=length(along_world);const auto along=std::isfinite(n) && n>1e-6f?rotate(frame.rotation,scale(along_world,1/n)):vec{};
		return {true,scale(compose(frame,{point,{0,0,0,1}}).position,1/units),rotate(frame.rotation,palm),scale(world,1/units),along};
	}
	// No IK/relaxed grasp input. A continuous exterior approach is required
	// again after cooldown, failed writes, deep penetration or lost contact.
	class cover_push
	{
		using clock=controller_input::clock;
		clock::time_point after_{},at_{};
		cover_push_contact previous_{};
		bool seen_{},armed_{},touching_{},coasting_{};
		float start_{},amount_{},speed_{},travel_{},coast_start_{},coast_target_{},coast_time_{},elapsed_{};
		const char* reason_{"idle"};
		void clear_motion()noexcept{seen_=armed_=touching_=coasting_=false;speed_=travel_=0;}
	public:
		const char* reason()const noexcept{return reason_;}
		void reset()noexcept{*this={};}
		void cancel()noexcept{clear_motion();reason_="manual part grasp";}
		void cooldown(clock::time_point now)noexcept{clear_motion();after_=now+std::chrono::milliseconds(300);reason_="cooldown";}
		template<class Move>bool update(const profile& p,state s,const cover_push_contact& c,
			clock::time_point now,bool open,Move&& move)
		{
			using namespace hands;
			if(!p.push || s.cover<=0 || !cover_available(s,p.bridge!=nullptr) || now<after_ || !c.valid ||
				!finite_part_vec(c.point) || !finite_part_vec(c.palm) || !finite_part_vec(c.world) || !finite_part_vec(c.along))
			{reason_=!p.push?"disabled":s.cover<=0?"closed":!cover_available(s,p.bridge!=nullptr)?"bridge interlock":now<after_?"cooldown":"invalid palm sample";clear_motion();return false;}
			const float dt=std::chrono::duration<float>(now-at_).count();
			if(seen_ && (dt<=0 || dt>.1f || length(sub(c.world,previous_.world))>.20f || length(sub(c.point,previous_.point))>.20f))
			{reason_="discontinuous tracking";clear_motion();return false;}
			const auto rotation=conjugate(hinge_pose({},p.cover_axis,p.cover_angle,s.cover).rotation);
			const auto geometry=query_push(p,s.cover,c);const auto point=geometry.point;
			auto before=rotate(rotation,previous_.point);
			const auto& patch=*p.push;
			const auto inside=[&](vec v){return push_inside(patch,v,geometry.along);};
			const float gap=geometry.gap;
			const bool facing=geometry.facing;
			const bool continuous=seen_;
			const auto world_step=length(sub(c.world,previous_.world));
			previous_=c;at_=now;seen_=true;
			if(coasting_)
			{
				reason_=coast_target_==0?"coasting":"settling";
				elapsed_+=dt;const auto t=std::clamp(elapsed_/coast_time_,0.f,1.f);
				const auto target=settled_cover(coast_target_+(coast_start_-coast_target_)*(1-t)*(1-t));
				if(target<s.cover && !move(target)){reason_="write rejected";clear_motion();}
				else if(t>=1 || target==0){reason_=target==0?"closed":"settled";clear_motion();}
				return true;
			}
			if(!touching_)
			{
				if(!open || !facing){reason_=!open?"hand closed or occupied":"palm facing away";clear_motion();return false;}
				if(geometry.inside && gap>=push_rearm_gap && gap<=push_outer_gap){armed_=true;reason_="armed outside";return false;}
				const float inward=before[2]-point[2];
				// Sweep the exterior-to-contact crossing. A fast hand may already
				// lie behind the OLD plane, while the lid should follow that stroke.
				const bool crossing=continuous && armed_ && before[2]-patch.surface>=push_contact_gap && gap<=push_contact_gap && inward>0;
				if(!crossing)
				{
					if(!geometry.inside || gap<-push_penetration || gap>push_outer_gap){reason_=!geometry.inside?"outside outer lever":"outside surface band";clear_motion();}
					else reason_=!continuous || !armed_?"needs exterior approach":"approaching contact";
					return false;
				}
				const auto hit=add(before,scale(sub(point,before),(before[2]-patch.surface-push_contact_gap)/inward));
				if(!inside(hit) || inward/dt<.06f || inward/dt>push_max_speed || world_step<inward*.5f)
				{reason_="invalid approach sweep";clear_motion();return false;}
				touching_=true;armed_=false;start_=amount_=s.cover;travel_=speed_=0;
				before=hit; // Count only the part of the frame after first contact.
			}
			const auto project=[&](vec v){return sub(v,scale(p.cover_axis,dot(v,p.cover_axis)));};
			const auto a=project(before);
			const auto angle_at=[&](vec v){const auto b=project(v);return std::atan2(dot(p.cover_axis,cross(a,b)),dot(a,b));};
			const auto angle=angle_at(point),distance=std::max(0.f,-angle)*length(project(point));
			if(std::abs(angle)>.8f || distance/dt>push_max_speed || world_step<distance*.5f){reason_="discontinuous or passive push";clear_motion();return false;}
			const auto follows=[&](float t){
				const auto v=add(before,scale(sub(point,before),t));auto probe=c;probe.point=rotate(conjugate(rotation),v);
				const auto next=std::clamp(amount_+std::min(0.f,angle_at(v))/p.cover_angle,0.f,1.f);
				const auto g=query_push(p,next,probe);
				return open && g.facing && g.inside && g.gap>=-push_penetration && g.gap<=push_rearm_gap;
			};
			const bool contact=follows(1);float portion=contact?1.f:0.f;
			// Keep motion up to the exit point, then decide inertia from that
			// motion rather than discarding the entire fast frame on contact loss.
			if(!contact && follows(0))
			{float lo=0,hi=1;for(int i=0;i<10;++i){const auto mid=(lo+hi)*.5f;if(follows(mid))lo=mid;else hi=mid;}portion=lo;}
			bool moved=false;
			if(portion>0)
			{
				const auto v=add(before,scale(sub(point,before),portion));const auto turn=std::min(0.f,angle_at(v));
				const auto travel=-turn*length(project(v));if(turn<0 || contact)speed_=travel/(dt*portion);travel_+=travel;
				amount_=std::clamp(amount_+turn/p.cover_angle,0.f,1.f);const auto target=settled_cover(amount_);
				if(target<s.cover){if(!move(target)){reason_="write rejected";clear_motion();return true;}s.cover=target;moved=true;}
			}
			if(s.cover==0){reason_="closed";clear_motion();return true;}
			if(contact){reason_="pushing";return true;}
			if(travel_>=.04f && (start_-s.cover)*p.cover_angle>=.34906585f && speed_>=.40f)
			{
				coasting_=true;touching_=armed_=false;coast_start_=s.cover;coast_target_=0;elapsed_=0;reason_="coasting";
				coast_time_=std::clamp(s.cover*p.cover_angle*std::max(.08f,-point[0])/speed_,.08f,.35f);return true;
			}
			if(travel_>=.008f && (start_-s.cover)*p.cover_angle>=.06f && speed_>=.03f)
			{
				// A slow push gets a small dissipating run-out, never the full-close
				// promise reserved for a committed fast stroke. Stopping on the lid
				// sets speed to zero above, so old momentum cannot be banked.
				const auto radians=std::min(.20943951f,speed_*.12f/std::max(.08f,-point[0]));
				coasting_=true;touching_=armed_=false;coast_start_=s.cover;
				coast_target_=std::max(0.f,s.cover-radians/p.cover_angle);coast_time_=.16f;elapsed_=0;reason_="settling";return true;
			}
			reason_="contact lost without momentum";clear_motion();return moved;
		}
	};
}
