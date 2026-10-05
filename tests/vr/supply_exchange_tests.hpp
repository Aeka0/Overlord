#pragma once
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/hand_interaction/core.hpp"
#include "component/vr/gameplay/native_ammunition_storage.hpp"
#include "component/vr/gameplay/underbarrel_feed.hpp"

namespace supply_exchange_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace m=w::mechanics;namespace u=w::underbarrel;
		namespace hi=vr::gameplay::hand_interaction;namespace storage=w::native_ammunition::storage;
		using vr::hand;
		const hi::edges held{false,false,true,true,1},press{true,false,true,true,2},release{false,true,false,true,2};
		check(hi::supply_selection(hi::domain::magazine,held,press,true)==hi::domain::underbarrel &&
			hi::supply_selection(hi::domain::underbarrel,held,release,true)==hi::domain::magazine,"Grip changes select the opposite waist supply while Trigger remains held");
		check(hi::supply_selection(hi::domain::magazine,held,press,false)==hi::domain::none &&
			hi::supply_selection(hi::domain::underbarrel,held,release,false)==hi::domain::none &&
			hi::supply_selection(hi::domain::magazine,held,held,true)==hi::domain::none,
			"outside changes and reentering the waist without a fresh Grip edge cannot switch");
		check(hi::supply_selection(hi::domain::magazine,release,press,true)==hi::domain::none &&
			hi::supply_selection(hi::domain::magazine,{false,false,true,false,1},press,true)==hi::domain::none,
			"Trigger release or unarmed reconnect cannot exchange ammunition");
		for(auto actor:{hand::left,hand::right})for(auto kind:{u::kind::m203,u::kind::gp25,u::kind::shotgun})for(int rounds:{0,7,30})
		{
			const auto rear=hand(1-int(actor));auto rules=w::m4::reload_rules;rules.discard_penalty=true;
			m::state primary{49,1,1,true,true,14,60,rounds,actor};
			auto secondary=u::import_native({{49,7},53,kind},u::capacity(kind),5);
			const auto primary_total=m::total_rounds(primary),secondary_total=u::total(secondary);
			std::array<std::byte,storage::extent> bytes{};
			check(storage::commit(bytes,1,1,0,0,15,60) && storage::commit(bytes,2,2,0,0,secondary.loaded,5),"seed independent primary and secondary native stores");
			const hi::grasp magazine{{hi::domain::magazine,{49,7},0,9},hi::role::supply,hi::button::trigger,hi::recipe::single,{}};
			const hi::grasp alternate{{hi::domain::underbarrel,{49,7},0,9},hi::role::supply,hi::button::trigger,hi::recipe::single,{}};
			hi::arbiter authority;authority.begin(1,1);authority.observe(actor,magazine);
			for(int step=0;step<8;++step)
			{
				const bool draw_primary=step%2;const auto& from=draw_primary?alternate:magazine;const auto& to=draw_primary?magazine:alternate;
				authority.begin(step+2,1);
				m::request q{draw_primary?m::operation::draw_magazine:m::operation::cancel_magazine,49,1,primary.revision,rear,actor};
				q.disposition=w::ammunition::disposition_reason::waist_return;
				const auto a=m::plan(rules,primary,q);
				const auto b=u::plan(secondary,{draw_primary?u::operation::cleanup:u::operation::draw,secondary.id,secondary.revision,rear,actor});
				const auto unchanged=bytes;const auto source_id=authority.find(actor,from.destination)->id;
				check(!authority.exchange_supply(actor,from.destination,to,[&]{return a && b && storage::commit_reserve_pair(bytes,
					{{{1,a.before.reserve,a.after.reserve},{2,b.before.reserve+1,b.after.reserve}}});}) && bytes==unchanged &&
					authority.find(actor,from.destination)->id==source_id,"failed second native comparison retains both pools and the original hand lease");
				check(authority.exchange_supply(actor,from.destination,to,[&]{return a && b && storage::commit_reserve_pair(bytes,
					{{{1,a.before.reserve,a.after.reserve},{2,b.before.reserve,b.after.reserve}}});}),"same host exchanges magazines and all supported secondary round types in either hand");
				if(!a || !b)continue;primary=a.next;secondary=b.next;
				check(m::total_rounds(primary)==primary_total && u::total(secondary)==secondary_total &&
					storage::observe(bytes,1,1).clip.count==15 && storage::observe(bytes,2,2).clip.count==u::capacity(kind) &&
					primary.chamber_loaded && secondary.chamber && !a.rounds_spent && !b.spent,
					"repeated switches preserve each feed budget and both chambers even with discard penalties enabled");
				check(!authority.find(actor,from.destination) && authority.find(actor,to.destination),"only the new supply owns the hand after commit");
			}
			auto wrong=alternate;wrong.destination.object.generation++;
			check(!authority.exchange_supply(actor,magazine.destination,wrong,[]{return true;}),"a supply exchange cannot cross physical host generations");
			secondary.reserve=0;const auto empty=u::plan(secondary,{u::operation::draw,secondary.id,secondary.revision,rear,actor});
			check(!authority.exchange_supply(actor,magazine.destination,alternate,[&]{return bool(empty);}) && authority.find(actor,magazine.destination),
				"empty secondary reserves retain the held primary magazine");
		}
		{
			Fixture f(&w::m4::physical);f.trigger(true);
			auto loaded=f.state;loaded.held_rounds=7;loaded.magazine_hand=hand::left;f.adopt(loaded);
			f.geometry.magazine_pose=1;
			f.control.exchange_supply(*f.tuning,f.geometry,f.input,f.owner,f.now,true);f.step();
			check(f.control.magazine_pose()==1 && f.state.held_rounds==7 && f.state.magazine_hand==hand::left,
				"externally exchanged magazine keeps its chosen grasp and Trigger lease on the next update");
		}
	}
}
