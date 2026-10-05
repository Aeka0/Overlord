#pragma once
#include "component/vr/gameplay/weapons/miniuzi/profile.hpp"
#include "component/vr/gameplay/weapons/miniuzi/mechanics.hpp"
#include "component/vr/gameplay/native_ammo_grant.hpp"
#include <limits>

namespace open_bolt_tests
{
	template <typename Check> void run(Check&& check)
	{
		using namespace vr::gameplay::weapons;
		namespace m=mechanics;
		const auto rules=miniuzi::reload_rules;
		for (int capacity:{1,32,1000}) for (int rounds=0;rounds<=capacity;++rounds)
		{
			const open_bolt::rules r{capacity}; open_bolt::state s;
			check(open_bolt::from_native_automatic(r,rounds,s) && s.magazine_rounds==rounds,
				"open feed admission keeps the entire native loaded total in the magazine");
			check(open_bolt::ready(r,s)==(rounds>0) && !open_bolt::ready(r,s,true),
				"open feed requires ammunition, cocking and a released action hand");
			int extracted=-1;
			check(open_bolt::extract(r,s,extracted) && extracted==0 && s.magazine_rounds==rounds &&
				s.action==action_state::held_open && !open_bolt::fire(r,s),"manual rear stroke never extracts or fires");
			check(!open_bolt::extract(r,s,extracted) && open_bolt::finish_stroke(r,s) &&
				s.action==action_state::cocked_open,"full stroke is idempotent until release and cocks even an empty magazine");
			check(open_bolt::cycle(r,s,extracted) && extracted==0 && s.magazine_rounds==rounds,
				"repeated full cocking cannot spend or feed ammunition");
			for (int left=rounds;left>0;--left)
				check(open_bolt::fire(r,s) && s.magazine_rounds==left-1 &&
					s.action==(left>1 ? action_state::cocked_open : action_state::closed),
					"each accepted shot consumes one and only the last shot closes the bolt");
			check(!open_bolt::fire(r,s),"empty open-bolt state never fires");
			const auto before=s;
			check(!open_bolt::from_native_automatic(r,capacity+1,s) && s==before &&
				!open_bolt::from_native_automatic(r,-1,s) && s==before,"open-bolt native admission rejects plus-one and negatives atomically");
		}
		for (int capacity:{0,1001,std::numeric_limits<int>::max()})
			check(!open_bolt::valid({capacity},{true,0,action_state::closed}),"open feed capacity is bounded");
		for (auto action:{action_state::locked_open,static_cast<action_state>(99)})
			check(!open_bolt::valid({32},{true,0,action}),"follower lock and unknown action cannot enter open-bolt policy");
		check(!open_bolt::valid({32},{false,1,action_state::cocked_open}),"open bolt cannot fire a detached magazine or chamber-only round");
		for (bool inserted:{false,true})
		{
			open_bolt::state empty{inserted,0,action_state::cocked_open};
			check(open_bolt::dry_fire({32},empty) && empty.action==action_state::closed && !open_bolt::dry_fire({32},empty),
				"empty trigger releases the open bolt once with empty or absent magazine");
		}
		for (auto action:{action_state::closed,action_state::held_open,action_state::cocked_open})
		{
			open_bolt::state live{true,32,action}; const auto before=live;
			check(!open_bolt::dry_fire({32},live) && live==before,"dry trigger cannot spend live ammunition or override a held action");
		}
		check(!closed_bolt::valid({32,true,true},{true,false,32,action_state::cocked_open}),
			"closed-bolt policy cannot inherit sear-ready state");

		for (auto rear:{hand::left,hand::right}) for (bool cock_first:{false,true})
		{
			m::state s{49,1,1,false,false,0,96};
			const auto apply=[&](m::operation op) {
				const auto actor=op==m::operation::release_button || op==m::operation::accepted_shot ? rear : static_cast<hand>(1-int(rear));
				const m::request req{op,s.weapon,s.instance_generation,s.revision,rear,actor};
				const auto tx=m::plan(rules,s,req); if (!tx) return false;
				check(tx.before==m::native_ammo(s) && m::total_rounds(s)==m::total_rounds(tx.next)+tx.rounds_spent &&
					!tx.next.chamber_loaded && !m::plan(rules,tx.next,req),"open-bolt transactions conserve ammo and reject revision replay");
				check(tx.rounds_spent==(op==m::operation::accepted_shot ? 1 : 0),"only firing spends Mini Uzi ammunition");
				s=tx.next; return true;
			};
			if (cock_first) check(apply(m::operation::cycle_action),"cock before fetching or inserting any magazine");
			check(!m::ready(rules,s) && !apply(m::operation::accepted_shot),"cocked without magazine cannot fire");
			check(apply(m::operation::draw_magazine) && !apply(m::operation::cycle_action) && apply(m::operation::insert_magazine),
				"pistol-well transfer excludes cocking with the occupied magazine hand");
			check(m::native_ammo(s).loaded==32 && !s.chamber_loaded && m::ready(rules,s)==cock_first,
				"insertion preserves prior cocking and never creates a chambered thirty-third round");
			if (!cock_first) check(apply(m::operation::cycle_action),"cock after insertion enables the same feed state");
			for (int i=0;i<3;++i) check(apply(m::operation::cycle_action) && m::native_ammo(s).loaded==32,"live repeated pulls retain all thirty-two rounds");
			check(apply(m::operation::release_button) && s.action==action_state::cocked_open && !m::ready(rules,s) &&
				!apply(m::operation::release_button),"magazine release neither closes nor releases the sear");
			check(apply(m::operation::draw_magazine) && apply(m::operation::insert_magazine) && m::ready(rules,s),
				"tactical replacement preserves cocking without an unnecessary extra rack");
			for (int i=0;i<32;++i) check(apply(m::operation::accepted_shot),"complete native automatic magazine");
			check(s.action==action_state::closed && !s.chamber_loaded && !m::ready(rules,s),"last shot closes without follower lock or chamber round");
			const auto grant=m::reconcile_native_grant(rules,s,{32,s.reserve_rounds},200);
			check(grant.valid && grant.next.action==action_state::closed && grant.after.loaded==0 && !grant.next.chamber_loaded,
				"native ammo grant goes to reserve and cannot recock or insert ammunition");
			check(apply(m::operation::cycle_action) && s.action==action_state::cocked_open && !m::ready(rules,s),
				"can recock over an empty magazine before removing it");
			check(apply(m::operation::release_button) && apply(m::operation::draw_magazine) && apply(m::operation::insert_magazine) && m::ready(rules,s),
				"empty-magazine cocking survives the later reload");
		}
		m::state s{49,1,1,true,false,32,64,0,hand::none,action_state::cocked_open};
		for (int field=0;field<3;++field)
		{
			auto invalid=rules;
			if (field==0) invalid.plus_one=true; if (field==1) invalid.release_control=true; if (field==2) invalid.last_round_lock=true;
			check(!m::valid(invalid,s),"open-bolt descriptor rejects contradictory closed-bolt capabilities");
		}
		s.chamber_loaded=true; check(!m::valid(rules,s),"Mini Uzi cannot persist a chamber round");
	}
}
