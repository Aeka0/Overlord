#pragma once
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/reload_item.hpp"
#include "component/vr/gameplay/falling_item_presenter.hpp"
#include "falling_rail_presentation_tests.hpp"
#include "component/vr/gameplay/reload_item_compatibility.hpp"
#include "component/vr/gameplay/hand_interaction/core.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"

namespace reload_item_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace items=vr::gameplay::reload_items;namespace w=vr::gameplay::weapons;namespace m=w::mechanics;
		namespace c=w::cylinder;namespace hi=vr::gameplay::hand_interaction;using vr::hand;
		using namespace vr::gameplay::hands;using namespace std::chrono_literals;
		falling_rail_presentation_tests::run(check);
		const auto born=items::clock::time_point{1s};items::flight motion;motion.start.position={0,0,40};motion.units=40;motion.born=born;
		using reason=w::ammunition::disposition_reason;
		for(bool penalty:{false,true})for(auto type:{items::kind::magazine,items::kind::speedloader})
		for(auto disposal:{reason::deliberate_discard,reason::waist_return,reason::forced_cleanup})for(int rounds:{0,1,6})
		{
			items::inventory pool;const auto id=pool.reserve(type,{49,7},rounds,30,1,motion);pool.activate(id,true);
			pool.catch_item(id,hand::left,born+10ms);pool.release(id,hand::left,motion);pool.catch_item(id,hand::right,born+20ms);
			check(pool.rounds()==rounds,"catching and transferring a penalized item preserves its exact ammunition");
			pool.expire(id,disposal);int returned{},writes{};const bool lost=penalty && disposal==reason::deliberate_discard;
			if(rounds && !lost)check(!pool.settle(id,[](auto,int){return false;},penalty) && pool.rounds()==rounds,
				"failed waist return or cleanup keeps the payload for a later native compare");
			check(pool.settle(id,[&](auto,int amount){returned+=amount;++writes;return true;},penalty) && !pool.find(id) &&
				returned==(lost?0:rounds) && writes==int(!lost && rounds>0),"only deliberate disposal loses all remaining rounds when enabled");
			check(!pool.settle(id,[&](auto,int){++writes;return true;},penalty),"loss and recovery both consume the item exactly once");
		}
		for(auto rear:{hand::left,hand::right})for(bool penalty:{false,true})for(bool waist:{false,true})
		{
			Fixture f(&w::m4::physical);auto rules=*f.rules;rules.discard_penalty=penalty;f.rules=&rules;
			f.owner.rear=rear;f.step();f.trigger(true);
			auto held=f.state;held.magazine_inserted=false;held.magazine_rounds=0;held.held_rounds=7;held.magazine_hand=hand(1-int(rear));f.adopt(held);
			f.geometry.waist_distance=waist?0.f:1.f;const auto reserve=f.native.reserve;
			f.writable=false;f.trigger(false);
			check(f.state.held_rounds==7 && f.native.reserve==reserve,"rejected discard or waist return retains the source magazine");
			f.writable=true;f.step();
			check(f.state.magazine_hand==hand::none && f.native.reserve==reserve+(penalty && !waist?0:7) &&
				f.state.chamber_loaded==held.chamber_loaded,"held magazine return recovers its rounds while an outside release applies the configured penalty");
		}
		{
			Fixture f(&w::m4::physical);auto rules=*f.rules;rules.discard_penalty=true;f.rules=&rules;
			f.trigger(true);auto held=f.state;held.magazine_inserted=false;held.magazine_rounds=0;held.held_rounds=7;held.magazine_hand=hand::left;f.adopt(held);
			check(f.control.interrupt(rules,f.state,f.owner.rear,[&](const auto& tx){return f.commit(tx);}) && f.native.reserve==held.reserve_rounds+7,
				"forced mechanical interruption still refunds held ammunition with the penalty enabled");
			const auto dropped=m::plan(rules,m::state{49,1,1,true,true,14,60},{m::operation::release_button,49,1,1,hand::right,hand::right});
			check(dropped && dropped.rounds_spent==14 && dropped.after==w::ammunition::projection{1,60},
				"missing world-item capacity loses only magazine contents while preserving the chamber and reserves");
		}
		{
			c::state cylinder{49,1,1,0,0,20,6,hand::left,c::action::open};
			for(auto disposal:{reason::deliberate_discard,reason::waist_return,reason::forced_cleanup})
			{
				c::request request{c::operation::discard,49,1,1,hand::right,hand::left};request.disposition=disposal;
				const auto tx=c::plan({6,true},cylinder,request);
				check(tx && tx.after.reserve==20+(disposal==reason::deliberate_discard?0:6) &&
					tx.rounds_spent==(disposal==reason::deliberate_discard?6:0),"speedloader fallback distinguishes actual discard from waist return and forced cleanup");
			}
			const auto cleanup=c::plan({6,true},cylinder,{c::operation::cleanup,49,1,1,hand::right,hand::left});
			check(cleanup && cleanup.after.reserve==26 && cleanup.rounds_spent==0 && cleanup.silent,
				"forced speedloader cleanup refunds its contents despite the penalty");
			const auto cleared=c::plan({6,true},c::state{49,1,1,3,3,20,0,hand::none,c::action::open},
				{c::operation::clear,49,1,1,hand::right,hand::right});
			check(cleared && cleared.after==w::ammunition::projection{0,20} && cleared.rounds_spent==3,
				"revolver clearing loses only live ammunition and never charges spent cases");
		}
		for(auto rear:{hand::left,hand::right})
		{
			Fixture f(&w::m4::physical);f.owner.rear=rear;f.step();f.manipulation=false;f.trigger(true);
			f.geometry.attached_magazine_pose=1;f.control.seat_external(f.geometry,f.input,f.owner,f.now);
			f.manipulation=true;f.step();check(f.control.magazine_seated() && f.control.magazine_pose()==1,
				"independent insertion initializes the pinch lease so continued Trigger holding keeps the seated grasp");
			f.trigger(false);check(!f.control.magazine_seated() && f.state.magazine_inserted,"release after independent insertion leaves the magazine in the weapon");
		}
		for(auto actor:{hand::left,hand::right})for(int rounds:{0,1,7,30})
		{
			items::inventory pool;m::state feed{49,1,1,true,true,5,60,rounds,actor};const auto rear=hand(1-int(actor));
			const auto total=m::total_rounds(feed);m::request request{m::operation::cancel_magazine,49,1,1,rear,actor};request.preserve_discard=true;
			const auto drop=m::plan(w::m4::reload_rules,feed,request);
			check(drop && drop.item_released && drop.rounds_to_item==rounds && drop.after.reserve==60,"discard reserves actual contents outside native reserve, including empty magazines");
			auto reservation=pool.reserve(items::kind::magazine,{49,7},rounds,30,1,motion);
			check(reservation && pool.rounds()==0,"uncommitted reservation cannot own ammunition");
			check(pool.activate(reservation,false) && !pool.find(reservation) && m::total_rounds(feed)==total,"native rejection rolls back the empty reservation without touching source ammunition");
			const auto id=pool.reserve(items::kind::magazine,{49,7},rounds,30,1,motion);check(id && id!=reservation,"reused pool slot has a fresh identity");
			pool.activate(id,true);feed=drop.next;check(m::total_rounds(feed)+pool.rounds()==total,"accepted source transfer conserves feed plus world-item payload");
			check(pool.catch_item(id,actor,born+10ms) && !pool.catch_item(id,rear,born+10ms),"either empty hand can catch, but two hands cannot acquire the same item");
			const auto token=pool.find(id)->revision;
			check(pool.find(id)->holder==actor && pool.rounds()==rounds,"held item remains independent of the origin weapon's holding state");
			for(int n=0;n<30;++n)
			{
				auto released=motion;released.born=born+std::chrono::milliseconds(20+n*20);
				check(pool.release(id,actor,released) && pool.catch_item(id,actor,released.born+10ms),"repeated release and catch retain one physical container");
				check(m::total_rounds(feed)+pool.rounds()==total && pool.find(id)->revision>token,"repeated interception cannot duplicate or refill rounds");
			}
			m::state recipient{53,8,1,false,false,0,20};m::request insert{m::operation::insert_external_magazine,53,8,1,rear,actor};insert.external_rounds=rounds;
			const auto before=m::total_rounds(recipient)+pool.rounds();const auto seated=m::plan(w::m4::reload_rules,recipient,insert);
			check(seated && seated.rounds_from_item==rounds && seated.next.magazine_rounds==rounds,"another compatible physical weapon accepts the exact recovered magazine contents");
			recipient=seated.next;check(pool.inserted(id,actor,rounds) && !pool.find(id) && m::total_rounds(recipient)+pool.rounds()==before,"successful insertion consumes the item once, including an empty one");
			check(!pool.inserted(id,actor,rounds) && !pool.catch_item(id,actor,born+10ms),"consumed item keys cannot replay insertion or recovery");
		}
		for(auto actor:{hand::left,hand::right})for(int rounds:{0,1,6})
		{
			items::inventory pool;c::state cylinder{49,1,1,0,0,20,rounds,actor,c::action::open};
			c::request request{c::operation::discard,49,1,1,hand(1-int(actor)),actor};request.preserve_discard=true;
			const auto dropped=c::plan({6},cylinder,request);check(dropped && dropped.item_released && dropped.rounds_to_item==rounds,"complete full, partial and empty speedloaders transfer as objects");
			const auto total=c::total_rounds(cylinder);const auto id=pool.reserve(items::kind::speedloader,{49,7},rounds,6,1,motion);pool.activate(id,true);cylinder=dropped.next;
			check(pool.catch_item(id,actor,born+20ms) && c::total_rounds(cylinder)+pool.rounds()==total,"caught speedloader preserves payload in either hand");
			c::request fill{c::operation::fill_external,49,1,cylinder.revision,hand(1-int(actor)),actor};fill.external_rounds=rounds;
			const auto tx=c::plan({6},cylinder,fill);
			if(rounds)
			{
				const auto pose_revision=pool.find(id)->pose_revision;
				check(tx && tx.next.live==rounds && tx.rounds_from_item==rounds,"recovered speedloader fills an open empty compatible cylinder");
				cylinder=tx.next;check(pool.inserted(id,actor,rounds) && pool.find(id)->state==items::phase::held && pool.find(id)->rounds==0,"filling keeps the actual empty loader held");
				check(pool.find(id)->pose_revision==pose_revision,"emptying a held loader does not invalidate its unchanged skinned grasp");
			}
			else check(!tx,"empty loader cannot manufacture cylinder rounds");
			check(c::total_rounds(cylinder)+pool.rounds()==total,"cylinder transfer conserves all rounds");
		}
		{
			items::inventory pool;const auto id=pool.reserve(items::kind::magazine,{49,7},11,30,1,motion);pool.activate(id,true);
			pool.advance(id,born-1ms);check(pool.find(id)->state==items::phase::falling,"a sample older than spawn cannot prematurely refund a newly released item");
			pool.advance(id,born+1200ms);check(pool.find(id)->state==items::phase::falling,"item remains catchable at the final lifetime boundary");
			pool.advance(id,born+1201ms);int native_reserve=20,writes{};
			check(!pool.settle(id,[&](auto,int){++writes;return false;}) && pool.rounds()==11,"failed expiry refund retains unsettled ammunition for retry");
			check(pool.settle(id,[&](auto origin,int amount){++writes;check(origin==w::weapon_identity{49,7},"refund preserves origin identity");native_reserve+=amount;return true;}) && native_reserve==31 && pool.rounds()==0,"expiry returns remaining rounds exactly once");
			check(!pool.settle(id,[&](auto,int){++writes;return true;}) && writes==2,"settled item cannot refund again");
		}
		{
			items::inventory pool;std::array<items::key,items::capacity> ids;
			for(size_t n=0;n<ids.size();++n){ids[n]=pool.reserve(items::kind::magazine,{49,7},1,30,1,motion);check(bool(ids[n]),"bounded item capacity admits available slots");pool.activate(ids[n],true);}
			check(!pool.room() && !pool.reserve(items::kind::magazine,{49,7},1,30,1,motion) && pool.rounds()==items::capacity,"pool overflow rejects without evicting unsettled payload");
			pool.catch_item(ids[0],hand::left,born+10ms);pool.rebase(2);
			check(pool.find(ids[0])->state==items::phase::held && pool.find(ids[0])->reference==2 && pool.find(ids[1])->state==items::phase::settling,"recenter retains held objects and settles obsolete world trajectories");
			check(pool.find(ids[1])->disposition==w::ammunition::disposition_reason::forced_cleanup,"tracking-reference cleanup stays distinct from a deliberate discard");
			pool.clear();const auto next=pool.reserve(items::kind::magazine,{49,8},0,30,2,motion);
			check(next && next!=ids[0] && !pool.find(ids[0]) && pool.rounds()==0,"checkpoint clearing cannot revive old item identities or refund old escrow");
		}
		{
			items::flight rail=motion;rail.rail={0,0,-4};rail.rail_seconds=.16f;rail.advance(born);
			check(std::abs(rail.pose(born+80ms).position[2]-38.f)<.0001f,"ejected magazine follows the authored rail before gravity");
			const auto frozen=rail;auto committed=rail;committed.advance(born+160ms);
			for(int hz:{45,90,144})
			{
				float previous=INFINITY;
				for(int frame=0;frame<hz;++frame)
				{
					const auto at=born+std::chrono::duration_cast<items::clock::duration>(std::chrono::duration<double>(double(frame)/hz));
					const auto displayed=frozen.pose(at),actual=committed.pose(at);
					check(length(sub(displayed.position,actual.position))<.0001f && displayed.position[2]<=previous,
						"an unrefreshed rail snapshot continues through gravity at render cadence without freezing or rebounding");
					previous=displayed.position[2];
				}
			}
			check(!frozen.detached && !rail.detached,"render sampling cannot commit mechanical rail departure");
			namespace fp=vr::gameplay::falling_item_presentation;
			fp::motion case_motion;case_motion.path=motion;case_motion.scattered=true;
			case_motion.scatter=w::ejection_scatter::sample(7,8,2);
			auto tip_motion=case_motion;tip_motion.local.position={1,2,3};
			const auto packed_at=born+240ms;
			const auto casing=case_motion.pose(packed_at),tip=tip_motion.pose(packed_at);
			check(length(sub(vr::gameplay::hands::pose_math::compose(casing,tip_motion.local).position,tip.position))<.0001f && casing.rotation==tip.rotation,
				"case and tip retain one shared flight and tumble sample at native surface packing");
			const auto next=case_motion.pose(packed_at+20ms),repeated=case_motion.pose(packed_at);
			check(next.position[2]<casing.position[2] && repeated.position==casing.position && repeated.rotation==casing.rotation,
				"a reused submission advances with packing time while duplicate eye or piece consumers do not integrate twice");
			auto moved=rail.start;moved.position[0]=12;rail.advance(born+160ms,&moved);
			const auto falling=rail.pose(born+260ms);
			check(rail.detached && falling.position[0]==12 && falling.position[2]<33.5f,"departure freezes the real rail exit and continues through the shared gravity trajectory");
			items::catch_probe probe;w::physical_reload::box_motion a,b;a.regions[0].half=a.regions[1].half={.015f,.02f,.06f};b=a;
			a.frame.position={-.2f,0,0};b.frame.position={.2f,0,0};
			check(!probe.test(1,a,born) && probe.test(1,b,born+20ms),"held-trigger interception detects a fast crossing even between nonoverlapping samples");
			probe={};a.frame.position={-1,0,0};b.frame.position={0,0,0};
			check(!probe.test(1,a,born) && !probe.test(1,b,born+20ms),"tracking teleport cannot synthesize a swept catch");
			b.frame.position[0]=NAN;check(!probe.test(1,b,born+30ms),"invalid tracking cannot capture or poison the next catch sample");
		}
		{
			hi::arbiter authority;authority.begin(1,1);
			const hi::target item{hi::domain::reload_item,{1,7}};
			for(auto actor:{hand::left,hand::right})authority.offer({actor,{item,hi::role::supply,hi::button::trigger,hi::recipe::single,{}},0,10,.01f,1,true,true});
			int caught{};authority.resolve([&](const auto&){++caught;return true;});check(caught==1,"central hand authority permits only one hand to intercept a shared object");
			hi::arbiter occupied;occupied.begin(1,1);occupied.observe(hand::left,{{hi::domain::carry,{49,8}},hi::role::control,hi::button::grip,hi::recipe::single,{}});
			occupied.offer({hand::left,{item,hi::role::supply,hi::button::trigger,hi::recipe::single,{}},0,10,0,1,true,true});caught=0;
			occupied.resolve([&](const auto&){++caught;return true;});check(!caught,"holding Trigger cannot steal an occupied hand to catch a falling item");
		}
		check(items::compatible(w::ak47::physical,w::ak47::arctic) && !items::compatible(w::m4::physical,w::ak47::physical),"recovery accepts reviewed compatible skins but never guesses cross-family magazine compatibility");
		check(!items::compatible(w::reload_profile{},w::reload_profile{}) && !items::compatible(w::cylinder_profile{},w::cylinder_profile{}),"empty descriptors cannot invent container compatibility");
		for(int invalid:{-1,31,INT_MAX})
		{
			items::inventory pool;check(!pool.reserve(items::kind::magazine,{49,7},invalid,30,1,motion),"invalid/extreme payload counts cannot enter the independent ledger");
		}
	}
}
