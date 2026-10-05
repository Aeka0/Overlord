#pragma once
#include "cylinder_gesture.hpp"

namespace vr::gameplay::weapons::lever
{
	using clock=controller_input::clock;
	struct tuning
	{
		float stroke{.85f}, spin_speed{5.f}, spin_angle{.30f}, spin_seconds{.65f};
		float catch_angle{.30f}, catch_speed{1.8f}, max_speed{35.f};
		std::span<const float> spin_open;
		float loading_open{.65f}, grip_separation{.65f}, return_seconds{.18f};
	};
	struct pose_state {float open{},spin{};bool spinning{},operating{};float entry_open{};bool grasped{true};bool returning{};};
	inline constexpr float catch_progress=.85f;
	inline bool catch_ready(pose_state pose)noexcept
	{return pose.spinning && pose.spin>=catch_progress && pose.open==0;}
	inline bool blocks_support(pose_state pose)noexcept
	{return pose.spinning && !catch_ready(pose);}
	inline bool closed_tail(const tuning& p,pose_state pose)noexcept
	{
		if(!pose.spinning || pose.open!=0 || p.spin_open.size()<2)return false;
		const auto next=size_t(std::ceil(pose.spin*float(p.spin_open.size()-1)));
		for(size_t i=next;i<p.spin_open.size();++i)if(p.spin_open[i]!=0)return false;
		return true;
	}

	inline bool valid(const tuning& p)noexcept
	{
		const float values[]{p.stroke,p.spin_speed,p.spin_angle,p.spin_seconds,p.catch_angle,p.catch_speed,p.max_speed,p.loading_open,p.grip_separation,p.return_seconds};
		for(const float x:values)if(!std::isfinite(x) || x<=0)return false;
		if(p.stroke>3 || p.spin_speed>=p.max_speed || p.spin_angle<.20f || p.spin_angle>1 ||
			p.spin_seconds<.2f || p.spin_seconds>3 || p.catch_angle>1 || p.max_speed>100 || p.spin_open.size()>64 || p.loading_open>=.95f || p.grip_separation>=1 || p.return_seconds>.5f)return false;
		for(const float x:p.spin_open)if(!std::isfinite(x) || x<0 || x>1)return false;
		return p.spin_open.empty() || (p.spin_open.size()>1 && p.spin_open.front()==0 && p.spin_open.back()==0);
	}
	// A bounded input controller. Its proposal is copied before feed commits so
	// rejected native transactions cannot advance a mechanical endpoint.
	class controller
	{
		pose_state pose_{};
		hands::quat previous_{0,0,0,1},start_{0,0,0,1};
		clock::time_point at_{};
		controller_input::consumer_continuity continuity_;

		cylinder::twist_gate flick_{};
		bool sampled_{},held_{},caught_{},open_hold_{};float catch_time_{},hold_quiet_{};
	public:
		pose_state pose()const noexcept{return pose_;}
		bool catch_confirmed()const noexcept{return caught_;}
		void close_endpoint()noexcept
		{pose_.open=0;if(pose_.returning){pose_.returning=pose_.operating=false;pose_.grasped=true;held_=false;}}
		void start_return()noexcept
		{pose_.spinning=false;pose_.spin=0;pose_.returning=true;pose_.operating=true;held_=false;caught_=false;flick_.reset();}
		void restore(float open)noexcept{const bool grasped=pose_.grasped;pose_={std::clamp(open,0.f,1.f)};pose_.grasped=grasped;}
		void bind_grasp(const tuning& p,const hold& owner)noexcept
		{interrupt();pose_.grasped=owner.attachment==control_attachment::moving || pose_.open<p.grip_separation;}
		void interrupt()noexcept
		{const auto saved=pose_;*this={};pose_.open=saved.open;pose_.grasped=saved.grasped;}
		void update(const tuning& p,const controller_input::frame& input,const hold& owner,bool movable,bool stop_at_open=false)noexcept
		{
			using namespace hands;
			if(!valid(p) || !valid_hand(owner.rear)){interrupt();return;}
			const int h=int(owner.rear);const auto& trigger=input.trigger[h];
			quat rotation{};
			if(!input.focused || !input.aim[h].valid || !input.grip[h].valid ||
				!input.squeeze[h].active || !input.squeeze[h].down || !trigger.active ||
				!cylinder::tracking_rotation(input.aim[h].tracking.orientation,rotation)) {interrupt();return;}
			const float dt=std::chrono::duration<float>(input.sampled_at-at_).count();
			const auto delta=normalize(multiply(conjugate(previous_),rotation));
			previous_=rotation;at_=input.sampled_at;
			// This is a server consumer, not the tracking producer. Slow motion
			// may consume fresh poses 200-250ms apart. Still bound the inferred
			// motion span and never bridge a producer/context discontinuity.
			if(continuity_.update(input,input.sampled_at) || (sampled_ && (dt<0 || dt>.5f))){interrupt();return;}
			const bool baseline=!sampled_ || dt<=0;sampled_=true;
			if(pose_.returning)
			{
				if(trigger.down){pose_.returning=pose_.operating=false;return;}
				if(baseline)return;
				pose_.open=std::max(0.f,pose_.open-std::min(dt,.05f)/p.return_seconds);
				if(pose_.open==0)close_endpoint();
				return;
			}
			if(trigger.down && closed_tail(p,pose_))
			{
				// The action is already shut. A new firing intent hands the gun
				// back immediately; an invisible flourish cannot postpone firing.
				pose_={};held_=caught_=false;flick_.reset();return;
			}
			if(!movable || !pose_.grasped || (trigger.down && !closed_tail(p,pose_))){held_=false;pose_.operating=false;flick_.reset();return;}
			const bool acquiring=!held_;if(acquiring){held_=true;start_=rotation;catch_time_=0;}
			pose_.operating=true;
			if(acquiring || baseline)return;
			auto q=delta;if(q[3]<0)for(auto& x:q)x=-x;
			const float full=2*std::atan2(length({q[0],q[1],q[2]}),q[3]);
			if(!std::isfinite(full) || full/dt>p.max_speed){interrupt();return;}
			if(open_hold_)
			{
				// Ignore the residual wrist recovery of the empty-gun flick. A
				// quiet tracked baseline returns control to deliberate manual motion.
				pose_.operating=false;
				hold_quiet_=full/dt<.8f?hold_quiet_+std::min(dt,.05f):0;
				if(hold_quiet_>=.06f){open_hold_=false;held_=false;}
				return;
			}
			// Tracking aim +X is pitch. Head motion, artificial turning, IK and
			// controller translation never fund a lever stroke or spin gesture.
			const float pitch=2*std::atan2(q[0],q[3]);
			cylinder::tuning flick;
			flick.twist_speed=p.spin_speed;flick.twist_angle=p.spin_angle;
			flick.twist_fast_angle=.20f;flick.twist_max_speed=p.max_speed;
			const bool spin_eligible=p.spin_open.size()>1 && owner.support==hand::none && !pose_.spinning && pose_.open<.65f;
			if(flick_.sample(flick,rotation,input.sampled_at,spin_eligible,{1,0,0},input.continuity_generation))
			{pose_.entry_open=pose_.open;pose_.spinning=true;pose_.spin=0;catch_time_=0;caught_=false;}
			if(!pose_.spinning)
			{pose_.open=std::clamp(pose_.open+pitch/p.stroke,0.f,1.f);return;}
			// A support grasp cancels assistance. The actual open mechanism remains
			// where it was, and can be completed by the ordinary wrist stroke.
			if(owner.support!=hand::none)
			{
				pose_.spinning=pose_.operating=false;pose_.spin=0;caught_=false;held_=false;flick_.reset();return;
			}
			if(pose_.spin<catch_progress)
			{
				const float next=std::min(catch_progress,pose_.spin+std::min(dt,.05f)/p.spin_seconds);
				if(stop_at_open)
				{
					for(size_t i=1;i<p.spin_open.size();++i)if(p.spin_open[i]>=1)
					{
						const float opening=float(i)/float(p.spin_open.size()-1);
						if(pose_.spin<=opening && next>=opening)
						{
							pose_={1};held_=caught_=false;flick_.reset();open_hold_=true;hold_quiet_=0;return;
						}
						break;
					}
				}
				pose_.spin=next;
				const float frame=pose_.spin*float(p.spin_open.size()-1);
				const auto i=std::min(size_t(frame),p.spin_open.size()-2);
				pose_.open=p.spin_open[i]+(p.spin_open[i+1]-p.spin_open[i])*(frame-float(i));
				pose_.open=std::max(pose_.open,pose_.entry_open*std::max(0.f,1-pose_.spin/(6.f/35.f)));
				return;
			}
			// Catch is a one-way handoff, not a condition that must remain true
			// throughout the final visual tail. A closed-action firing intent also
			// ends the flourish; the feed still requires neutral Trigger rearming.
			if(!caught_)
			{
				const auto relative=normalize(multiply(conjugate(start_),rotation));
				const float angle=2*std::acos(std::clamp(std::abs(relative[3]),0.f,1.f));
				const bool stable=angle<=p.catch_angle && full/dt<=p.catch_speed;
				catch_time_=stable?catch_time_+std::min(dt,.05f):0;
				caught_=catch_time_>=.06f || (trigger.down && closed_tail(p,pose_));
			}
			if(!caught_)return;
			pose_.spin=std::min(1.f,pose_.spin+std::min(dt,.05f)/p.spin_seconds);
			pose_.open=0;
			if(pose_.spin>=1){pose_={};held_=false;caught_=false;flick_.reset();}
		}
	};
}
