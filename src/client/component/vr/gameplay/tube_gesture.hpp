#pragma once
#include "tube_feed.hpp"
#include "physical_reload_gesture.hpp"
#include "lever_gesture.hpp"

namespace vr::gameplay::weapons::tube
{
	using clock=controller_input::clock;
	struct loading_extension
	{
		// Sweep the original contact sphere sideways and down. Its upper bound
		// stays fixed; this also preserves every previously accepted contact.
		float sideways{},down{};
		float distance(hands::vec delta)const noexcept
		{
			delta[0]=std::max(0.f,std::abs(delta[0])-sideways);
			delta[1]=std::max(0.f,std::abs(delta[1])-sideways);
			delta[2]=delta[2]<0 ? std::min(0.f,delta[2]+down) : delta[2];
			return hands::length(delta);
		}
	};
	struct tuning { physical_reload::profile rack; float port_radius{.045f},tube_radius{.045f},alignment{.35f}; loading_extension loading{}; lever::tuning lever{}; };
	struct geometry
	{
		bool valid{};std::uint32_t weapon{};std::uint64_t instance_generation{},reference_generation{},input_sequence{};
		float waist_distance{10},rack_distance{10};hands::vec hand_in_gun{},port_delta{},tube_delta{};
		float port_alignment{},tube_alignment{};std::uint8_t rack_pose{};
	};
	inline bool pump_supported(const controller_input::frame& input,const hold& owner) noexcept
	{
		if(!owner.can_fire())return false;
		const int off=1-int(owner.holding_hand());const auto& squeeze=input.squeeze[off];
		return owner.support==hand(off) && squeeze.active && squeeze.down;
	}
	inline bool pump_release_held(const controller_input::frame& input,const hold& owner,bool rack_held) noexcept
	{
		if(!valid_hand(owner.holding_hand()))return false;
		const int rear=int(owner.holding_hand()),off=1-rear;
		const auto& main=input.secondary[rear];const auto& other=input.secondary[off];
		return (main.active && main.down) || ((pump_supported(input,owner) || rack_held) && other.active && other.down);
	}
	inline bool pump_movable(const tuning& p,const state& s,float travel,bool release) noexcept
	{
		// At the closed loaded endpoint the controller clears its cycle latch
		// unless the release is held. All other valid pump phases are movable.
		return !s.chamber || s.phase==action::held_open || travel>p.rack.close_travel || release;
	}
	inline bool rack_acquirable(const tuning& p,rules r,const state& s,const geometry& g,float travel,bool release) noexcept
	{
		return !rotary(r) && !levered(r) && s.loader_hand==hand::none && g.rack_pose<p.rack.slide_pose_count &&
			g.rack_distance<=p.rack.slide_radius && (!manual(r) || pump_movable(p,s,travel,release));
	}
	class controller
	{
		controller_input::digital_press_gate take_{},close_{};std::uint64_t sequence_{},reference_{},instance_{},rear_revision_{};
		std::uint64_t pinch_generation_{},fire_generation_{};
		hand rear_{hand::none};bool armed_{},rack_held_{},contact_armed_{};
		controller_input::consumer_continuity continuity_;
		physical_reload::slide_constraint rack_grip_{};float travel_{};hands::vec previous_{};
		bool cycle_unlock_{},return_release_required_{};lever::controller lever_{};
		const char* decision_{"waiting for tube scene"};
	public:
		bool fire_armed()const noexcept{return armed_;}bool rack_held()const noexcept{return rack_held_;}
		void restore_travel(float value)noexcept{travel_=std::isfinite(value) && value>=0 ? value : 0;cycle_unlock_=travel_>0;}
		float travel()const noexcept{return travel_;}auto rack_grip()const noexcept{return rack_grip_;}
		lever::pose_state lever_pose()const noexcept{return lever_.pose();}
		const char* decision()const noexcept{return decision_;}
		template<class Commit> bool interrupt(rules r,state& s,hand rear,Commit&& write)
		{
			take_={};close_={};return_release_required_=false;sequence_=0;armed_=false;rack_held_=false;contact_armed_=false;lever_.interrupt();
			if(!manual(r) || travel_==0)cycle_unlock_=false;
			const auto apply=[&](operation op,hand actor){const auto tx=plan(r,s,{op,s.weapon,s.instance_generation,s.revision,rear,actor});if(!ammunition::commit(tx,write))return false;s=tx.next;return true;};
			if(s.loader_hand!=hand::none && !apply(operation::cleanup,s.loader_hand))return false;
			if(!manual(r) && s.phase==action::held_open && valid_hand(rear) && !apply(operation::rack_close,other_hand(rear)))return false;
			return true;
		}
		template<class Commit> void update(const tuning& p,rules r,state& s,const controller_input::frame& input,
			const hold& owner,const geometry& g,bool gameplay,clock::time_point now,Commit&& write,hand_interaction::access access={})
		{
			const bool manipulation=access.manipulation,acquire=access.acquire;
			if(!valid(r,s) || !valid_hand(owner.holding_hand()) || owner.weapon!=s.weapon){armed_=false;return;}
			const auto rear=owner.holding_hand();const auto off=other_hand(rear);const int h=int(off);
			auto pinch=input.trigger[h];if(access.release)pinch.down=false;const auto& fire=input.trigger[int(rear)];
			const bool fresh=physical_reload::valid(p.rack) && gameplay && input.focused && input.sequence && input.reference_generation &&
				now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150) && input.grip[int(rear)].valid &&
				g.valid && g.weapon==s.weapon && g.instance_generation==s.instance_generation && g.reference_generation==input.reference_generation &&
				g.input_sequence==input.sequence && physical_reload::finite(g.hand_in_gun) && physical_reload::finite(g.port_delta) && physical_reload::finite(g.tube_delta) &&
				std::isfinite(g.waist_distance) && g.waist_distance>=0 && std::isfinite(g.rack_distance) && g.rack_distance>=0 &&
				std::isfinite(g.port_alignment) && std::isfinite(g.tube_alignment);
			if(!fresh){decision_="stale scene/tracking";(void)interrupt(r,s,rear,write);return;}
			const bool control_changed=instance_!=s.instance_generation || rear_!=rear || rear_revision_!=owner.rear_revision;
			if(continuity_.update(input,now) || instance_!=s.instance_generation || rear_!=rear || rear_revision_!=owner.rear_revision || reference_!=input.reference_generation ||
				pinch_generation_!=pinch.generation || fire_generation_!=fire.generation ||
				input.sequence<sequence_)
				if(!interrupt(r,s,rear,write))return;
			if(levered(r) && control_changed)
			{lever_.restore(travel_/p.rack.slide_stroke);lever_.bind_grasp(p.lever,owner);}
			if(sequence_==input.sequence)return;
			sequence_=input.sequence;instance_=s.instance_generation;rear_=rear;rear_revision_=owner.rear_revision;reference_=input.reference_generation;
			pinch_generation_=pinch.generation;fire_generation_=fire.generation;
			const auto apply=[&](operation op){const auto tx=plan(r,s,{op,s.weapon,s.instance_generation,s.revision,rear,levered(r) && (op==operation::rack_open || op==operation::rack_close)?rear:off,cycle_unlock_});if(!ammunition::commit(tx,write)){decision_="tube transaction rejected";return false;}s=tx.next;return true;};
			if(levered(r)){lever_update(p,r,s,input,owner,g,access,apply);return;}
			if(pumped(r))
			{
				armed_=owner.can_fire() && fire.active && (armed_ || !fire.down);
				pump_update(p,r,s,input,owner,g,access,apply);return;
			}
			const bool available=manipulation && input.grip[h].valid && input.aim[h].valid && pinch.active && owner.support!=off;
			const bool local_press=take_.consume(pinch);const bool pressed=available && access.pinch.value_or(local_press);if(!available)take_={};
			if(!available){(void)interrupt(r,s,rear,write);return;}
			armed_=owner.can_fire() && fire.active && (armed_ || !fire.down);
			if(rack_held_)
			{
				const auto delta=hands::sub(g.hand_in_gun,rack_grip_.start);const auto axial=hands::dot(delta,p.rack.slide_axis);
				const bool separated=hands::length(hands::sub(delta,hands::scale(p.rack.slide_axis,axial)))>p.rack.slide_lateral_limit ||
					hands::length(hands::sub(g.hand_in_gun,previous_))>p.rack.max_contact_step;
				previous_=g.hand_in_gun;
				if(!pinch.down || separated){rack_held_=false;if(s.phase==action::held_open)(void)apply(operation::rack_close);return;}
				travel_=physical_reload::constrained_slide_travel(p.rack,rack_grip_,g.hand_in_gun);
				if(travel_>=p.rack.full_stroke && s.phase!=action::held_open)
					if(!apply(operation::rack_open)){rack_held_=false;return;}
				if(travel_<=p.rack.close_travel && s.phase==action::held_open)(void)apply(operation::rack_close);
				decision_="tube rack held";return;
			}
			travel_=s.phase==action::locked_open ? p.rack.locked_travel : 0;
			if(s.loader_hand!=hand::none){load_shell(p,r,s,pinch,g,apply);return;}

			if(!acquire || !pressed || !pinch.down)return;
			if(access.parts && rack_acquirable(p,r,s,g,travel_,false)){rack_held_=true;rack_grip_={g.hand_in_gun,travel_,g.rack_pose};previous_=g.hand_in_gun;return;}
			if(access.supply && g.waist_distance<=p.rack.waist_radius){(void)apply(operation::draw);previous_=g.hand_in_gun;contact_armed_=false;}
		}
	private:
		template<class Apply> void load_shell(const tuning& p,rules r,state& s,
			const controller_input::digital_action& pinch,const geometry& g,Apply&& apply)
		{
			if(!pinch.down){(void)apply(operation::discard);return;}
			const float port=p.loading.distance(g.port_delta),bottom=p.loading.distance(g.tube_delta);
			const bool jump=hands::length(hands::sub(g.hand_in_gun,previous_))>p.rack.max_contact_step;previous_=g.hand_in_gun;
			if(jump){contact_armed_=false;return;}
			if(port>p.port_radius+.02f && bottom>p.tube_radius+.02f)contact_armed_=true;
			if(!contact_armed_)return;
			const float loading_travel=levered(r)?p.lever.loading_open*p.rack.slide_stroke:p.rack.full_stroke;
			const bool side=(rotary(r) ? s.stored<r.capacity : s.phase==(manual(r) ? action::held_open : action::locked_open) && !s.chamber) &&
				(!manual(r) || travel_>=loading_travel) && port<=p.port_radius && g.port_alignment>=p.alignment;
			const bool below=!rotary(r) && (!levered(r) || (s.phase==action::held_open && travel_>=loading_travel)) && bottom<=p.tube_radius && g.tube_alignment>=p.alignment && s.stored<r.capacity;
			if(side || below){contact_armed_=false;(void)apply(side && (!below || port/p.port_radius<=bottom/p.tube_radius) ? operation::load_port : operation::load_tube);}
		}
		template<class Apply> void lever_update(const tuning& p,rules r,state& s,const controller_input::frame& input,
			const hold& owner,const geometry& g,hand_interaction::access access,Apply&& apply)
		{
			const int rear=int(owner.holding_hand()),off=1-rear;
			hands::quat tracked{};
			if(!lever::valid(p.lever) || !input.aim[rear].valid || !input.squeeze[rear].active || !input.squeeze[rear].down ||
				!input.trigger[rear].active || !cylinder::tracking_rotation(input.aim[rear].tracking.orientation,tracked))
			{armed_=false;lever_.interrupt();return;}
			auto pinch=input.trigger[off];if(access.release)pinch.down=false;
			const bool available=access.manipulation && owner.manipulation_hand()==hand(off) && input.grip[off].valid && input.aim[off].valid && pinch.active;
			const bool pressed=take_.consume(pinch);if(!available)take_={};
			const bool close_pressed=close_.consume(input.secondary[rear]);
			if(s.loader_hand!=hand::none)
			{
				lever_.interrupt();armed_=false;
				if(available)load_shell(p,r,s,pinch,g,apply);else (void)apply(operation::cleanup);
				return;
			}
			// Transfer/lifecycle can restore travel without preserving a gesture.
			if(std::abs(lever_.pose().open*p.rack.slide_stroke-travel_)>.0001f)lever_.restore(travel_/p.rack.slide_stroke);
			if(!owner.can_fire())
			{
				// Fore-end carry keeps the gun and open lever, but frees the former
				// control hand for the same centrally arbitrated shell transactions.
				armed_=false;lever_.interrupt();
				if(available && access.acquire && access.supply && access.pinch.value_or(pressed) && pinch.down && g.waist_distance<=p.rack.waist_radius)
				{(void)apply(operation::draw);previous_=g.hand_in_gun;contact_armed_=false;}
				decision_="support-only carry; free hand may load shells";return;
			}
			const bool release_down=input.secondary[rear].active && input.secondary[rear].down;
			if(!release_down)return_release_required_=false;
			const bool release=release_down && !return_release_required_;
			if(s.chamber && s.phase==action::closed && travel_<=p.rack.close_travel && !release)cycle_unlock_=false;
			cycle_unlock_=cycle_unlock_ || release;
			const bool movable=pump_movable(p,s,travel_,release) || lever_.pose().spinning;
			auto proposed=lever_;
			if(close_pressed && !lever_.pose().grasped && travel_>0 && !input.trigger[rear].down)
				{proposed.start_return();return_release_required_=true;}
			proposed.update(p.lever,input,owner,movable,!s.chamber && s.stored==0);
			auto motion=proposed.pose();float next=motion.open*p.rack.slide_stroke;
			// Snap a returning action to the actual closed pose before relocking;
			// the tolerance must never leave a visibly ajar but locked lever.
			if(next<=p.rack.close_travel && (next<travel_ || (s.chamber && s.phase==action::closed && !release && !motion.spinning)))
			{proposed.close_endpoint();motion=proposed.pose();next=0;}
			const float extraction=p.lever.loading_open*p.rack.slide_stroke;
			if((next>=extraction && s.phase!=action::held_open && !apply(operation::rack_open)) ||
				(next<=p.rack.close_travel && s.phase==action::held_open && !apply(operation::rack_close)))
			{lever_.interrupt();armed_=false;decision_="lever endpoint transaction rejected";return;}
			const bool closed=ready(r,s) && next<=p.rack.close_travel && !motion.returning &&
				(!motion.spinning || lever::closed_tail(p.lever,motion));
			lever_=proposed;travel_=next;rack_held_=false;
			if(!closed)armed_=false;
			else if(input.trigger[rear].active && !input.trigger[rear].down)armed_=true;
			// A deliberate supply acquisition pauses lever motion on the following frame.
			if(!motion.spinning && !motion.returning && available && access.acquire && access.supply && access.pinch.value_or(pressed) && pinch.down && g.waist_distance<=p.rack.waist_radius)
			{(void)apply(operation::draw);previous_=g.hand_in_gun;contact_armed_=false;}
			decision_=motion.returning?"fixed grip; lever returning":motion.spinning?(lever_.catch_confirmed()?"lever spin completing catch":"lever spin awaiting catch"):!motion.grasped?"fixed grip; lever retained":motion.operating?"unlocked lever follows holding wrist":"loaded closed lever locked";
		}
		template<class Apply> void pump_update(const tuning& p,rules r,state& s,const controller_input::frame& input,
			const hold& owner,const geometry& g,hand_interaction::access access,Apply&& apply)
		{
			const bool manipulation=access.manipulation,acquire=access.acquire;
			const auto rear=owner.holding_hand(),off=other_hand(rear);const int h=int(off);
			auto pinch=input.trigger[h];if(access.release)pinch.down=false;
			const bool tracked=input.grip[h].valid && input.aim[h].valid;
			const bool support=pump_supported(input,owner);
			const bool available=tracked && (manipulation || support) && pinch.active;
			const bool local_press=take_.consume(pinch);const bool pressed=available && access.pinch.value_or(local_press);if(!available)take_={};
			const bool release=pump_release_held(input,owner,rack_held_);
			if(!available){rack_held_=false;if(s.loader_hand!=hand::none)(void)apply(operation::cleanup);decision_="pump released; travel retained";return;}
			if(s.loader_hand!=hand::none){load_shell(p,r,s,pinch,g,apply);return;}
			if(s.chamber && s.phase==action::closed && travel_<=p.rack.close_travel && !release)
			{cycle_unlock_=false;rack_held_=false;}
			if(rack_held_)
			{
				const auto delta=hands::sub(g.hand_in_gun,rack_grip_.start);const auto axial=hands::dot(delta,p.rack.slide_axis);
				const bool separated=hands::length(hands::sub(delta,hands::scale(p.rack.slide_axis,axial)))>p.rack.slide_lateral_limit ||
					hands::length(hands::sub(g.hand_in_gun,previous_))>p.rack.max_contact_step;
				previous_=g.hand_in_gun;
				if((!support && !pinch.down) || separated){rack_held_=false;decision_="pump released; travel retained";return;}
				const float next=physical_reload::constrained_slide_travel(p.rack,rack_grip_,g.hand_in_gun);
				if(next>=p.rack.full_stroke && s.phase!=action::held_open && !apply(operation::rack_open))
				{rack_held_=false;return;}
				if(next<=p.rack.close_travel && travel_>p.rack.close_travel)
				{
					if(s.phase==action::held_open && !apply(operation::rack_close)){rack_held_=false;return;}
					// Keep the physical grasp and unlock while the release is held,
					// including across partial returns and consecutive full cycles.
					travel_=0;rack_held_=release;cycle_unlock_=release;
					decision_=release ? "pump closed; release held" : "pump closed and relocked";return;
				}
				travel_=next;decision_="pump follows supporting hand";return;
			}
			const bool can_move=pump_movable(p,s,travel_,release);
			if((support && can_move) || (acquire && access.parts && pressed && pinch.down && rack_acquirable(p,r,s,g,travel_,release)))
			{
				cycle_unlock_=cycle_unlock_ || release;
				rack_held_=true;rack_grip_={g.hand_in_gun,travel_,support ? std::uint8_t{0} : g.rack_pose};previous_=g.hand_in_gun;return;
			}
			if(acquire && access.supply && !support && pressed && pinch.down && g.waist_distance<=p.rack.waist_radius)
			{(void)apply(operation::draw);previous_=g.hand_in_gun;contact_armed_=false;}
			decision_=can_move ? "pump unlocked; awaiting grasp" : "loaded pump locked";
		}
		static hand other_hand(hand h)noexcept{return h==hand::left ? hand::right : hand::left;}
	};
}
