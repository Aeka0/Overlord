#pragma once
#include "ammunition_transfer.hpp"
#include "weapon_holding.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace vr::gameplay::weapons::break_action
{
	struct rules { unsigned capacity{}; };
	enum class action { closed, opening, open, closing };
	struct state
	{
		std::uint32_t weapon{};std::uint64_t instance_generation{},revision{};
		unsigned live{},spent{}; // Chamber masks; a partial Ranger reload retains the other live barrel.
		int reserve{},held_rounds{};hand loader_hand{hand::none};
		action phase{action::closed};float hinge{}; // 0 latched, 1 fully open.
	};
	inline unsigned mask(rules r) noexcept {return r.capacity<=2 ? (1u<<r.capacity)-1 : 0;}
	inline ammunition::projection native_ammo(const state& s) noexcept {return {std::popcount(s.live),s.reserve};}
	inline std::int64_t total_rounds(const state& s) noexcept {return std::int64_t(std::popcount(s.live))+s.reserve+s.held_rounds;}
	inline bool valid(rules r,const state& s) noexcept
	{
		return r.capacity>0 && r.capacity<=2 && s.weapon && s.instance_generation && s.revision &&
			!((s.live|s.spent)&~mask(r)) && !(s.live&s.spent) && s.reserve>=0 && s.held_rounds>=0 && s.held_rounds<=1 &&
			(s.loader_hand==hand::none ? s.held_rounds==0 : valid_hand(s.loader_hand) && s.held_rounds==1) &&
			std::isfinite(s.hinge) && s.hinge>=0 && s.hinge<=1 && (s.phase!=action::closed || s.hinge==0) &&
			(s.phase==action::closed || s.phase==action::opening || s.phase==action::open || s.phase==action::closing) &&
			total_rounds(s)<=std::numeric_limits<int>::max();
	}
	inline bool ready(rules r,const state& s) noexcept {return valid(r,s) && s.phase==action::closed && s.live;}
	inline state import_native(rules r,std::uint32_t weapon,std::uint64_t generation,ammunition::projection native) noexcept
	{
		if(!r.capacity || r.capacity>2 || native.loaded<0 || native.loaded>int(r.capacity))return {};
		const auto live=(1u<<native.loaded)-1;
		state s{weapon,generation,1,live,mask(r)&~live,native.reserve};return valid(r,s) ? s : state{};
	}
	enum class operation { open, move, close, draw, load, discard, cleanup, accepted_shot, quick_load };
	enum class effect { none, open, eject, close, draw, load, discard, shot };
	struct request
	{operation op{};std::uint32_t weapon{};std::uint64_t instance_generation{},revision{};hand rear{hand::none},actor{hand::none};unsigned chamber{};float hinge{};bool sustained{};};
	struct transaction
	{
		bool accepted{};state next{};ammunition::projection before{},after{};effect feedback{};
		unsigned ejected{},chamber{};int rounds_spent{};bool silent{};
		explicit operator bool() const noexcept{return accepted;}
	};
	inline transaction plan(rules r,const state& s,request q) noexcept
	{
		transaction tx;
		if(!valid(r,s) || q.weapon!=s.weapon || q.instance_generation!=s.instance_generation || q.revision!=s.revision ||
			s.revision==UINT64_MAX || !valid_hand(q.rear) || !valid_hand(q.actor))return tx;
		const bool off=q.op==operation::draw || q.op==operation::load || q.op==operation::discard || q.op==operation::cleanup;
		if(off ? q.actor==q.rear : q.actor!=q.rear)return tx;
		auto n=s;
		switch(q.op)
		{
		case operation::open:
			if(n.phase!=action::closed)return tx;n.phase=action::opening;tx.feedback=effect::open;break;
		case operation::move:
			if(n.phase==action::closed || !std::isfinite(q.hinge) || q.hinge<0 || q.hinge>1 || q.hinge==n.hinge ||
				(n.phase==action::opening && q.hinge<n.hinge) || (n.phase==action::closing && q.hinge>n.hinge))return tx;
			n.hinge=q.hinge;
			if(n.phase==action::opening && n.hinge==1)
			{n.phase=action::open;tx.ejected=n.spent;n.spent=0;if(tx.ejected)tx.feedback=effect::eject;}
			else if(n.hinge==0)
			{n.phase=action::closed;tx.feedback=effect::close;}
			if(tx.feedback==effect::none)tx.silent=true;break;
		case operation::close:
			if(n.phase!=action::open)return tx;n.phase=action::closing;tx.silent=true;break;
		case operation::draw:
			if(n.loader_hand!=hand::none || !n.reserve)return tx;
			--n.reserve;n.held_rounds=1;n.loader_hand=q.actor;tx.feedback=effect::draw;break;
		case operation::load:
			if(n.phase!=action::open || n.hinge<.98f || q.chamber>=r.capacity || n.loader_hand!=q.actor ||
				!n.held_rounds || ((n.live|n.spent)&(1u<<q.chamber)))return tx;
			n.live|=1u<<q.chamber;n.held_rounds=0;n.loader_hand=hand::none;tx.chamber=q.chamber;tx.feedback=effect::load;break;
		case operation::quick_load:
		{
			if(n.phase!=action::open || n.hinge!=1 || n.live || n.spent || n.reserve<=0)return tx;
			const int rounds=std::min(int(r.capacity),n.reserve);
			n.live=(1u<<rounds)-1;n.reserve-=rounds;tx.feedback=effect::load;break;
		}
		case operation::discard:case operation::cleanup:
			if(n.loader_hand!=q.actor)return tx;
			n.reserve+=ammunition::dispose(n.held_rounds,q.op==operation::cleanup ? ammunition::disposition_reason::forced_cleanup : ammunition::disposition_reason::deliberate_discard).returned;
			n.held_rounds=0;n.loader_hand=hand::none;tx.feedback=effect::discard;tx.silent=q.op==operation::cleanup;break;
			case operation::accepted_shot:
				if(!ready(r,n))return tx;
				if(q.sustained){tx.chamber=std::countr_zero(n.live);tx.feedback=effect::shot;break;}
			tx.chamber=std::countr_zero(n.live);n.live&=~(1u<<tx.chamber);n.spent|=1u<<tx.chamber;
			tx.rounds_spent=1;tx.feedback=effect::shot;break;
		default:return tx;
		}
		++n.revision;
		if(!valid(r,n) || total_rounds(s)!=total_rounds(n)+tx.rounds_spent)return tx;
		tx.accepted=true;tx.next=n;tx.before=native_ammo(s);tx.after=native_ammo(n);return tx;
	}
}
