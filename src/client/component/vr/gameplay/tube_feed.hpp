#pragma once
#include "ammunition_transfer.hpp"
#include "weapon_holding.hpp"
#include <limits>

namespace vr::gameplay::weapons::tube
{
	enum class feed_layout { tube, fixed_drum };
	enum class action_drive { automatic, pump, lever };
	struct rules { int capacity{}; action_drive drive{}; feed_layout layout{feed_layout::tube}; bool chamber_bonus{true}; };
	inline bool manual(rules r)noexcept{return r.drive!=action_drive::automatic;}
	inline bool pumped(rules r)noexcept{return r.drive==action_drive::pump;}
	inline bool levered(rules r)noexcept{return r.drive==action_drive::lever;}
	inline bool rotary(rules r)noexcept{return r.layout==feed_layout::fixed_drum;}
	inline int loaded_capacity(rules r)noexcept{return r.capacity+int(!rotary(r) && r.chamber_bonus);}
	enum class action { closed, locked_open, held_open };
	struct state
	{
		std::uint32_t weapon{};std::uint64_t instance_generation{},revision{};
		int stored{},reserve{},held_rounds{};bool chamber{};
		hand loader_hand{hand::none};action phase{action::closed};
		bool spent_case{}; // Manually cycled guns retain the fired case until full extraction.
		unsigned drum_index{}; // Physical indexing is instance-owned, never animation time.
	};
	inline ammunition::projection native_ammo(const state& s) noexcept { return {s.stored+int(s.chamber),s.reserve}; }
	inline std::int64_t total_rounds(const state& s) noexcept { return std::int64_t(s.stored)+s.chamber+s.reserve+s.held_rounds; }
	inline bool valid(rules r,const state& s) noexcept
	{
		return (r.drive==action_drive::automatic || r.drive==action_drive::pump || r.drive==action_drive::lever) && r.capacity>0 && r.capacity<=64 && s.weapon && s.instance_generation && s.revision &&
			s.stored>=0 && s.stored<=r.capacity && native_ammo(s).loaded<=loaded_capacity(r) && s.reserve>=0 && s.held_rounds>=0 && s.held_rounds<=1 &&
			(s.loader_hand==hand::none ? s.held_rounds==0 : valid_hand(s.loader_hand) && s.held_rounds==1) &&
			(s.phase==action::closed || s.phase==action::locked_open || s.phase==action::held_open) &&
			(s.phase==action::closed || !s.chamber || (manual(r) && s.phase==action::held_open)) &&
			(!s.spent_case || (manual(r) && !s.chamber && s.phase==action::closed)) &&
			(!manual(r) || s.phase!=action::locked_open) &&
			(r.layout==feed_layout::tube || r.layout==feed_layout::fixed_drum) &&
			(rotary(r) ? !manual(r) && !s.chamber && !s.spent_case && s.phase==action::closed && s.drum_index<unsigned(r.capacity) : s.drum_index==0) &&
			total_rounds(s)<=std::numeric_limits<int>::max();
	}
	inline bool ready(rules r,const state& s) noexcept {return valid(r,s) && (rotary(r) ? s.stored>0 : s.chamber) && s.phase==action::closed;}
	inline state import_native(rules r,std::uint32_t weapon,std::uint64_t generation,ammunition::projection native) noexcept
	{
		state s{weapon,generation,1,std::max(0,native.loaded-1),native.reserve,0,native.loaded>0,hand::none,
			native.loaded || manual(r) ? action::closed : action::locked_open};
		if(rotary(r)){s.stored=native.loaded;s.chamber=false;s.phase=action::closed;}
		return native.loaded>=0 && valid(r,s) ? s : state{};
	}
	enum class operation { draw, load_tube, load_port, rack_open, rack_close, discard, cleanup, accepted_shot };
	enum class effect { none, draw, load_tube, load_port, rack_open, rack_close, discard, shot };
	struct request {operation op{};std::uint32_t weapon{};std::uint64_t instance_generation{},revision{};hand rear{hand::none},actor{hand::none};bool unlock{};
		hand firing_hand{hand::none}; // Explicit module authority; does not change the host's rear hand.
		bool sustained{};
	};
	struct transaction
	{
		bool accepted{};state next{};ammunition::projection before{},after{};effect feedback{};
		int rounds_spent{},ejected{};bool silent{},case_ejected{};
		explicit operator bool() const noexcept{return accepted;}
	};
	inline transaction plan(rules r,const state& s,request q) noexcept
	{
		transaction tx;
		if(!valid(r,s) || q.weapon!=s.weapon || q.instance_generation!=s.instance_generation || q.revision!=s.revision ||
			s.revision==UINT64_MAX || !valid_hand(q.rear) || !valid_hand(q.actor)) return tx;
		const bool shot=q.op==operation::accepted_shot;
		const bool cycle=q.op==operation::rack_open || q.op==operation::rack_close;
		if(q.op!=operation::cleanup && (shot ? q.actor!=(valid_hand(q.firing_hand)?q.firing_hand:q.rear) :
			levered(r) && cycle ? q.actor!=q.rear : q.actor==q.rear)) return tx;
		auto n=s;
		switch(q.op)
		{
		case operation::draw:
			if(n.loader_hand!=hand::none || !n.reserve) return tx;
			--n.reserve;n.held_rounds=1;n.loader_hand=q.actor;tx.feedback=effect::draw;break;
		case operation::load_tube:
		case operation::load_port:
			if(n.loader_hand!=q.actor || n.held_rounds!=1 || (!manual(r) && n.phase==action::held_open)) return tx;
			if(rotary(r))
			{
				if(q.op!=operation::load_port || n.stored>=r.capacity)return tx;
				++n.stored;n.drum_index=(n.drum_index+1)%unsigned(r.capacity);tx.feedback=effect::load_port;
			}
			else if(q.op==operation::load_tube)
			{if(n.stored==r.capacity || (levered(r) && n.phase!=action::held_open)) return tx;++n.stored;tx.feedback=effect::load_tube;}
			else
			{if(n.phase!=(manual(r) ? action::held_open : action::locked_open) || n.chamber) return tx;
			 n.chamber=true;if(!manual(r))n.phase=action::closed;tx.feedback=effect::load_port;}
			n.held_rounds=0;n.loader_hand=hand::none;break;
		case operation::rack_open:
			if(rotary(r))return tx;
			if(n.phase==action::held_open || n.loader_hand!=hand::none) return tx;
			if(manual(r) && n.chamber && !q.unlock) return tx;
			tx.case_ejected=n.spent_case;n.spent_case=false;
			tx.ejected=int(n.chamber);tx.rounds_spent=tx.ejected;n.chamber=false;n.phase=action::held_open;tx.feedback=effect::rack_open;break;
		case operation::rack_close:
			if(rotary(r))return tx;
			if(n.phase!=action::held_open) return tx;
			if(!n.chamber && n.stored){--n.stored;n.chamber=true;}
			n.phase=n.chamber || manual(r) ? action::closed : action::locked_open;
			tx.feedback=effect::rack_close;break;
		case operation::discard:
		case operation::cleanup:
			if(n.loader_hand!=q.actor) return tx;
			n.reserve+=ammunition::dispose(n.held_rounds,q.op==operation::cleanup ? ammunition::disposition_reason::forced_cleanup : ammunition::disposition_reason::deliberate_discard).returned;
			n.loader_hand=hand::none;n.held_rounds=0;tx.feedback=effect::discard;tx.silent=q.op==operation::cleanup;break;
			case operation::accepted_shot:
				if(!ready(r,n)) return tx;
				if(q.sustained){tx.feedback=effect::shot;break;}
			tx.rounds_spent=1;
			if(rotary(r)){--n.stored;n.drum_index=(n.drum_index+1)%unsigned(r.capacity);}
			else if(manual(r)){n.chamber=false;n.spent_case=true;}
			else if(n.stored)--n.stored;else {n.chamber=false;n.phase=action::locked_open;}
			tx.feedback=effect::shot;break;
		default:return tx;
		}
		++n.revision;
		if(!valid(r,n) || total_rounds(s)!=total_rounds(n)+tx.rounds_spent) return tx;
		tx.accepted=true;tx.next=n;tx.before=native_ammo(s);tx.after=native_ammo(n);return tx;
	}
}
