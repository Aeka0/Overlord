#pragma once
#include "break_action_feed.hpp"
#include "cylinder_gesture.hpp"
#include "physical_reload_geometry.hpp"

namespace vr::gameplay::weapons::break_action
{
	using clock=controller_input::clock;
	struct tuning
	{
		float opening_seconds{.25f},closing_seconds{.18f},open_angle{.75f};
		float waist_radius{.22f},load_radius{.045f},load_behind{.10f},load_inside{.055f},alignment_cosine{.25f};
		float barrel_radius{.11f},separation{.30f},max_step{.25f};
		cylinder::tuning flick=[] {cylinder::tuning t;t.twist_speed=6.f;t.twist_fast_angle=.20f;return t;}();
	};
	struct geometry
	{
		bool valid{};std::uint32_t weapon{};std::uint64_t instance_generation{},reference_generation{},input_sequence{};
		float waist_distance{10},barrel_distance{10},barrel_angle{};
		hands::vec barrel_hand{};std::array<hands::vec,2> shell_in_chamber{};
		std::array<float,2> alignment{};
	};
	inline bool valid(const tuning& t) noexcept
	{
		for(float v:{t.opening_seconds,t.closing_seconds,t.open_angle,t.waist_radius,t.load_radius,t.load_behind,t.load_inside,t.barrel_radius,t.separation,t.max_step})
			if(!std::isfinite(v) || v<=0 || v>3)return false;
		return std::isfinite(t.alignment_cosine) && t.alignment_cosine>-1 && t.alignment_cosine<=1 && cylinder::valid(t.flick);
	}
	inline bool contact(const tuning& t,const geometry& g,unsigned n) noexcept
	{return n<2 && cylinder::finite(g.shell_in_chamber[n]) && g.alignment[n]>=t.alignment_cosine &&
		physical_reload::sweep_well(g.shell_in_chamber[n],g.shell_in_chamber[n],t.load_radius,t.load_inside,t.load_behind);}
	inline bool barrel_acquirable(const tuning& t,const state& s,const geometry& g) noexcept
	{return s.phase==action::open && s.loader_hand==hand::none && g.barrel_distance<=t.barrel_radius;}
	class controller
	{
		controller_input::digital_press_gate release_{},pinch_{};cylinder::twist_gate flick_{};
		std::uint64_t sequence_{},reference_{},instance_{},rear_revision_{},pinch_generation_{},fire_generation_{};
		hand rear_{hand::none};clock::time_point at_{};bool armed_{},barrel_held_{},load_armed_{};
		controller_input::consumer_continuity continuity_;
		float start_angle_{},start_hinge_{};hands::vec previous_{};
		const char* decision_{"waiting for tracked break action"};
	public:
		bool fire_armed() const noexcept{return armed_;}bool barrel_held() const noexcept{return barrel_held_;}
		const char* decision() const noexcept{return decision_;}
		template<class Commit> bool interrupt(rules r,state& s,hand rear,Commit&& write)
		{
			release_={};pinch_={};flick_.reset();sequence_=0;armed_=barrel_held_=load_armed_=false;
			if(s.loader_hand==hand::none)return true;
			const auto tx=plan(r,s,{operation::cleanup,s.weapon,s.instance_generation,s.revision,rear,s.loader_hand});
			if(!ammunition::commit(tx,write))return false;s=tx.next;return true;
		}
		template<class Commit> void update(const tuning& t,rules r,state& s,const controller_input::frame& input,
			const hold& owner,const geometry& g,bool gameplay,clock::time_point now,Commit&& write,hand_interaction::access access={})
		{
			const bool manipulation=access.manipulation,acquire=access.acquire;
			if(!valid(r,s) || !valid_hand(owner.holding_hand()) || owner.weapon!=s.weapon){armed_=false;return;}
			const auto rear=owner.holding_hand(),off=hand(1-int(rear));auto take=input.trigger[int(off)];if(access.release)take.down=false;const auto& fire=input.trigger[int(rear)];
			bool fresh=valid(t) && gameplay && input.focused && input.sequence && !input.orientation_settling && input.grip[int(rear)].valid && input.aim[int(rear)].valid &&
				now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150) && g.valid && g.weapon==s.weapon && g.instance_generation==s.instance_generation &&
				g.reference_generation==input.reference_generation && g.input_sequence==input.sequence && cylinder::finite(g.barrel_hand) &&
				std::isfinite(g.barrel_angle) && std::isfinite(g.waist_distance) && g.waist_distance>=0 && std::isfinite(g.barrel_distance) && g.barrel_distance>=0;
			for(unsigned n=0;n<r.capacity;++n)fresh=fresh && cylinder::finite(g.shell_in_chamber[n]) && std::isfinite(g.alignment[n]);
			if(!fresh){decision_="stale/invalid break-action scene";(void)interrupt(r,s,rear,write);return;}
			if(continuity_.update(input,now) || instance_!=s.instance_generation || rear_!=rear || rear_revision_!=owner.rear_revision || reference_!=input.reference_generation ||
				pinch_generation_!=take.generation || fire_generation_!=fire.generation || input.sequence<sequence_)
				if(!interrupt(r,s,rear,write))return;
			if(sequence_==input.sequence)return;
			const float dt=sequence_ ? std::clamp(std::chrono::duration<float>(input.sampled_at-at_).count(),0.f,.15f) : 0;
			sequence_=input.sequence;reference_=input.reference_generation;instance_=s.instance_generation;rear_=rear;rear_revision_=owner.rear_revision;
			pinch_generation_=take.generation;fire_generation_=fire.generation;at_=input.sampled_at;
			const auto apply=[&](operation op,hand actor,unsigned chamber=0,float hinge=0.f){const auto tx=plan(r,s,{op,s.weapon,s.instance_generation,s.revision,rear,actor,chamber,hinge});
				if(!ammunition::commit(tx,write)){decision_="native break-action transaction rejected";return false;}s=tx.next;return true;};
			const bool opened=release_.consume(input.secondary[int(rear)]) && owner.can_fire();
			if(opened && s.phase==action::closed){(void)apply(operation::open,rear);armed_=false;flick_.reset();}
			if(dt>0 && (s.phase==action::opening || s.phase==action::closing))
			{
				const float target=s.phase==action::opening ? std::min(1.f,s.hinge+dt/t.opening_seconds) : std::max(0.f,s.hinge-dt/t.closing_seconds);
				if(!apply(operation::move,rear,0,target)){armed_=false;return;}
			}
			const bool available=manipulation && owner.support!=off && input.grip[int(off)].valid && input.aim[int(off)].valid && take.active;
			const bool local_press=pinch_.consume(take);const bool pressed=available && access.pinch.value_or(local_press);if(!available)pinch_={};
			if(!available)
			{
				barrel_held_=load_armed_=false;
				if(s.loader_hand!=hand::none && !apply(operation::cleanup,s.loader_hand)){armed_=false;return;}
			}
			if(available && s.loader_hand!=hand::none)
			{
				if(!take.down)(void)apply(operation::discard,off);
				else
				{
					bool touching=false;for(unsigned n=0;n<r.capacity;++n)touching=touching || contact(t,g,n);
					const bool continuous=hands::length(hands::sub(g.barrel_hand,previous_))<=t.max_step;previous_=g.barrel_hand;
					if(!continuous)load_armed_=false;
					else if(!touching)load_armed_=true;
					if(continuous && load_armed_ && s.phase==action::open && s.hinge>=.98f)
					{
						unsigned best=2;float distance=10;
						for(unsigned n=0;n<r.capacity;++n)if(!((s.live|s.spent)&(1u<<n)) && contact(t,g,n))
						{const auto d=hands::length(g.shell_in_chamber[n]);if(d<distance){best=n;distance=d;}}
						if(best<2){(void)apply(operation::load,off,best);load_armed_=false;}
					}
				}
			}
			else if(available && barrel_held_)
			{
				if(!take.down || s.phase!=action::open || g.barrel_distance>t.separation || hands::length(hands::sub(g.barrel_hand,previous_))>t.max_step)barrel_held_=false;
				else
				{
					const float angle=std::remainder(g.barrel_angle-start_angle_,6.283185307f);
					float target=std::clamp(start_hinge_+angle/t.open_angle,0.f,1.f);if(target<.04f)target=0;
					if(target!=s.hinge && !apply(operation::move,rear,0,target))barrel_held_=false;
					if(s.phase==action::closed)barrel_held_=false;
					previous_=g.barrel_hand;
				}
			}
			else if(acquire && available && pressed && take.down)
			{
				if(access.parts && barrel_acquirable(t,s,g))
				{barrel_held_=true;start_angle_=g.barrel_angle;start_hinge_=s.hinge;previous_=g.barrel_hand;}
				else if(access.supply && g.waist_distance<=t.waist_radius)
				{if(apply(operation::draw,off)){previous_=g.barrel_hand;load_armed_=true;for(unsigned n=0;n<r.capacity;++n)if(contact(t,g,n))load_armed_=false;}}
			}
			hands::quat rotation;
			const bool inertia=s.phase==action::open && !barrel_held_ && s.loader_hand==hand::none && owner.support==hand::none;
			if(cylinder::tracking_rotation(input.aim[int(rear)].tracking.orientation,rotation))
			{if(flick_.sample(t.flick,rotation,input.sampled_at,inertia,{1,0,0},input.continuity_generation))(void)apply(operation::close,rear);}
			else flick_.reset();
			armed_=s.phase==action::closed && owner.can_fire() && fire.active && (armed_ || !fire.down);
			decision_=barrel_held_ ? "barrel follows auxiliary hand" : s.phase==action::open ? "breech open; load or close" :
				s.phase==action::closed ? "breech latched" : "hinge moving";
		}
	};
}
