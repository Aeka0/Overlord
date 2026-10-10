#pragma once
#include "tube_feed.hpp"
#include "weapon_identity.hpp"

namespace vr::gameplay::weapons::underbarrel
{
	enum class kind { none, m203, gp25, shotgun };
	struct identity
	{
		weapon_identity host{}; std::uint32_t definition{}; kind type{};
		bool operator==(const identity&) const = default;
		explicit operator bool()const noexcept{return bool(host) && host.weapon<512 && definition && definition<512 && definition!=host.weapon &&
			(type==kind::m203 || type==kind::gp25 || type==kind::shotgun);}
	};
	// The host owns the chassis and rear hand. A module never impersonates an
	// inventory weapon or promotes its firing hand to the host's rear grip.
	struct state
	{
		identity id{};std::uint64_t revision{1};
		int loaded{},reserve{},held{};bool chamber{},spent{},open{};
		hand loader{hand::none};
	};
	inline constexpr int shotgun_tube_capacity=3;
	inline int capacity(kind k)noexcept{return k==kind::shotgun ? shotgun_tube_capacity+1 : k==kind::none ? 0 : 1;}
	inline constexpr float action_open_fraction=.9f;
	inline std::int64_t total(const state& s)noexcept{return std::int64_t(s.loaded)+s.reserve+s.held;}
	inline bool valid(const state& s)noexcept
	{
		return s.id && s.revision && s.revision!=UINT64_MAX && s.loaded>=0 && s.loaded<=capacity(s.id.type) &&
			s.reserve>=0 && s.reserve<=1000000 && s.held>=0 && s.held<=1 && total(s)<=1000000 &&
			(s.loader==hand::none ? !s.held : valid_hand(s.loader) && s.held==1) &&
			(!s.chamber || s.loaded>0) && !(s.chamber && s.spent) && !(s.open && s.spent) &&
			(s.id.type==kind::shotgun || s.loaded==int(s.chamber));
	}
	inline state import_native(identity id,int loaded,int reserve)noexcept
	{state s{id,1,loaded,reserve,0,loaded>0};return valid(s) ? s : state{};}
	inline bool ready(const state& s)noexcept{return valid(s) && s.chamber && !s.open && !s.spent;}
	enum class observed_change { unchanged, reserve, credit, debit, invalid };
	struct reconciliation
	{
		observed_change change{observed_change::invalid};state next{};ammunition::projection native_after{};
		explicit operator bool()const noexcept{return change!=observed_change::invalid;}
	};
	inline reconciliation reconcile(const state& s,identity id,ammunition::projection native)noexcept
	{
		if(!valid(s) || s.id!=id || native.loaded<0 || native.loaded>capacity(id.type) || native.reserve<0 || native.reserve>1000000)return {};
		auto n=s;auto change=observed_change::unchanged;
		if(native.loaded>s.loaded)
		{
			// Stock refill may move reserve into a clip. Preserve the observed
			// total budget, but never manufacture a physical loading/cycling action.
			const auto reserve=std::int64_t(native.reserve)+native.loaded-s.loaded;
			if(reserve>1000000)return {};n.reserve=int(reserve);change=observed_change::credit;
		}
		else if(native.loaded<s.loaded)
		{
			// A script/pickup/native debit is not proof of a VR shot. Accept its
			// reduced budget without refunding it, inventing a spent case, or
			// permanently disabling grasp/loading. An unconfirmed chamber must
			// be cycled before the next pump shot. Existing open/spent/escrow stay.
			n.loaded=native.loaded;n.chamber=false;n.reserve=native.reserve;change=observed_change::debit;
		}
		else if(native.reserve!=s.reserve){n.reserve=native.reserve;change=observed_change::reserve;}
		if(change!=observed_change::unchanged)++n.revision;
		if(!valid(n) || total(n)!=std::int64_t(native.loaded)+native.reserve+s.held)return {};
		return {change,n,{n.loaded,n.reserve}};
	}
	enum class operation { shot, draw, insert, open, close, discard, cleanup };
	struct request
	{
		operation op{};identity id{};std::uint64_t revision{};hand rear{hand::none},actor{hand::none};
		bool firing_contact{},unlock{},sustained{};
	};
	struct transaction
	{
		bool accepted{};state next{};ammunition::projection before{},after{};operation op{};
		bool ejected{};int spent{};explicit operator bool()const noexcept{return accepted;}
	};
	inline transaction plan(const state& s,request q)noexcept
	{
		transaction tx;
		if(!valid(s) || q.op<operation::shot || q.op>operation::cleanup || q.id!=s.id || q.revision!=s.revision || !valid_hand(q.rear) || !valid_hand(q.actor) || q.actor==q.rear)return tx;
		if(s.id.type==kind::shotgun)
		{
			// Reuse the shared pump/escrow conservation core. The module wrapper
			// authorizes the off hand, caps total loaded at four, and owns identity.
			if(q.op==operation::shot && (!q.firing_contact || s.held))return tx;
			tube::state feed{s.id.host.weapon,s.id.host.generation,s.revision,s.loaded-int(s.chamber),s.reserve,s.held,s.chamber,s.loader,
				s.open?tube::action::held_open:tube::action::closed,s.spent};
			const auto op=q.op==operation::shot?tube::operation::accepted_shot:q.op==operation::draw?tube::operation::draw:
				q.op==operation::insert?(s.open&&!s.chamber?tube::operation::load_port:tube::operation::load_tube):
				q.op==operation::open?tube::operation::rack_open:q.op==operation::close?tube::operation::rack_close:
				q.op==operation::cleanup?tube::operation::cleanup:tube::operation::discard;
			const auto inner=tube::plan({shotgun_tube_capacity,tube::action_drive::pump},feed,{op,s.id.host.weapon,s.id.host.generation,s.revision,q.rear,q.actor,q.unlock,q.actor,q.sustained});
			if(!inner)return tx;
			auto n=s;n.revision=inner.next.revision;n.loaded=inner.after.loaded;n.reserve=inner.next.reserve;n.held=inner.next.held_rounds;
			n.chamber=inner.next.chamber;n.spent=inner.next.spent_case;n.open=inner.next.phase==tube::action::held_open;n.loader=inner.next.loader_hand;
			if(!valid(n))return tx;
			return {true,n,inner.before,inner.after,q.op,inner.case_ejected||inner.ejected>0,inner.rounds_spent};
		}
		auto n=s;tx.op=q.op;
		switch(q.op)
		{
		case operation::shot:
			if(!q.firing_contact || !ready(s) || s.held)return tx;
			if(q.sustained)break;
			--n.loaded;n.chamber=false;n.spent=s.id.type!=kind::gp25;tx.spent=1;break;
		case operation::draw:
			if(s.held || !s.reserve)return tx;
			--n.reserve;n.held=1;n.loader=q.actor;break;
		case operation::insert:
			if(s.loader!=q.actor || s.loaded>=capacity(s.id.type))return tx;
			if(s.spent || (s.id.type==kind::m203 && !s.open))return tx;
			n.chamber=true;
			++n.loaded;n.held=0;n.loader=hand::none;break;
		case operation::open:
			if(s.id.type==kind::gp25 || s.open || s.held || (s.chamber && !q.unlock))return tx;
			tx.ejected=s.spent;n.spent=false;n.open=true;
			if(s.chamber){n.chamber=false;--n.loaded;tx.spent=1;tx.ejected=true;}break;
		case operation::close:
			if(!s.open || s.held)return tx;
			n.open=false;if(s.id.type==kind::shotgun && s.loaded)n.chamber=true;break;
		case operation::discard:case operation::cleanup:
			if(!s.held || s.loader!=q.actor)return tx;
			{
				const auto disposition=ammunition::dispose(s.held,q.op==operation::cleanup ? ammunition::disposition_reason::forced_cleanup : ammunition::disposition_reason::deliberate_discard);
				n.reserve+=disposition.returned;tx.spent=disposition.lost;
			}
			n.held=0;n.loader=hand::none;break;
		default:return tx;
		}
		++n.revision;
		if(!valid(n) || total(s)!=total(n)+tx.spent)return tx;
		tx.accepted=true;tx.next=n;tx.before={s.loaded,s.reserve};tx.after={n.loaded,n.reserve};return tx;
	}

	// A fresh pinch chooses exactly one route. Exchanging an existing supply
	// requires a separate waist-bound Grip edge and an atomic escrow transfer.
	enum class trigger_route { none, existing_lease, fire, part, primary_supply, secondary_supply };
	inline trigger_route route(bool fresh,bool occupied,bool firing,bool part,bool waist,bool secondary,bool installed)noexcept
	{
		if(occupied)return trigger_route::existing_lease;
		if(!fresh)return trigger_route::none;
		if(firing)return trigger_route::fire;
		if(part)return trigger_route::part;
		if(waist)return secondary && installed ? trigger_route::secondary_supply : trigger_route::primary_supply;
		return trigger_route::none;
	}
}
