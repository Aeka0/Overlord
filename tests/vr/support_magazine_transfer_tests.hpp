#pragma once
#include "component/vr/gameplay/hand_interaction/core.hpp"
#include "component/vr/gameplay/hand_interaction/pose_plan.hpp"

namespace support_magazine_transfer_tests
{
	template<class Check> void run(Check check)
	{
		namespace hi=vr::gameplay::hand_interaction;using hi::hand;
		for(auto actor:{hand::left,hand::right})
		{
			const hi::grasp support{{hi::domain::carry,{49,7},1},hi::role::support,hi::button::grip,hi::recipe::single,hi::capability::aim};
			const hi::grasp magazine{{hi::domain::magazine,{49,7},0,3},hi::role::supply,hi::button::trigger,hi::recipe::single,{}};
			for(bool accept:{false,true})
			{
				hi::arbiter authority;authority.begin(1,1);check(authority.observe(actor,support),"support exists before magazine transfer");
				hi::candidate request{actor,magazine,4,10,0,1,true,true,support.destination};
				authority.begin(2,1);authority.offer(request);int commits{};
				authority.resolve([&](const auto&){++commits;return accept;});
				check(commits==1 && bool(authority.find(actor,magazine.destination))==accept && bool(authority.find(actor,support.destination))!=accept,
					"support replacement is atomic, with no double occupancy or lost support on failed commit");
				const auto pose=hi::compose_pose(actor,authority.sessions());
				check(pose.driver.provider==(accept?hi::domain::magazine:hi::domain::carry),"pose owner follows the accepted transfer only");
				authority.begin(3,1);authority.offer(request);authority.resolve([&](const auto&){++commits;return true;});
				check(commits==1,"success or rejection consumes the transfer event");
			}
			for(int invalid=0;invalid<5;++invalid)
			{
				hi::arbiter authority;authority.begin(1,1);auto old=support;auto next=magazine;
				if(invalid==0)old.purpose=hi::role::control;
				if(invalid==1)++next.destination.object.generation;
				if(invalid==2)next.maintained=hi::button::grip;
				if(invalid==3)next.destination.provider=hi::domain::cylinder;
				authority.observe(actor,old);
				if(invalid==4)authority.release(authority.find(actor,old.destination)->id,false);
				authority.begin(2,1);authority.offer({actor,next,8,10,0,1,true,true,old.destination});int commits{};
				authority.resolve([&](const auto&){++commits;return true;});
				check(commits==0 && authority.find(actor,old.destination),"transfer cannot steal firing grip, another instance, wrong input/domain or an unsettled lease");
			}
			// Runtime reservations are provisional. Native failure witnesses the
			// original carry support and restores it at the same finish boundary.
			hi::arbiter provisional;provisional.begin(1,1);provisional.observe(actor,support);
			provisional.offer({actor,magazine,9,10,0,1,true,true,support.destination});provisional.resolve([](const auto&){return true;});
			provisional.retain([&](const auto& s){return s.held.destination==support.destination;});
			check(provisional.observe(actor,support) && !provisional.find(actor,magazine.destination),"rejected native transfer restores authoritative support without a ghost magazine");
			{
				hi::arbiter holes;holes.begin(1,1);auto other=support;other.purpose=hi::role::control;
				holes.observe(hand(1-int(actor)),other);holes.observe(actor,support);
				holes.release(holes.find(hand(1-int(actor)),other.destination)->id,true);
				holes.offer({actor,magazine,10,10,0,1,true,true,support.destination});holes.resolve([](const auto&){return true;});
				check(holes.find(actor,magazine.destination) && !holes.find(actor,support.destination),"replacement reuses the source slot even when an earlier session slot is free");
			}

			hi::input_history history;vr::controller_input::frame input;input.sequence=input.reference_generation=1;input.focused=true;
			input.sampled_at=hi::clock::time_point{std::chrono::seconds(1)};
			for(int h=0;h<2;++h)input.squeeze[h]=input.trigger[h]=input.secondary[h]={true,false,0,1};
			history.update(input,true,input.sampled_at);const auto rear=hand(1-int(actor));
			++input.sequence;input.secondary[int(rear)].down=true;++input.secondary[int(rear)].presses;
			input.squeeze[int(actor)].down=true;++input.squeeze[int(actor)].presses;
			history.update(input,true,input.sampled_at);
			check(history.get(rear,hi::button::secondary).press && history.get(actor,hi::button::grip).down &&
				history.get(rear,hi::button::secondary).event!=history.get(actor,hi::button::grip).event,
				"rear secondary and offhand grip have independent globally unique events");
			++input.sequence;history.update(input,true,input.sampled_at);
			check(!history.get(rear,hi::button::secondary).press,"held release button cannot manufacture another transfer edge");
			++input.sequence;++input.secondary[int(rear)].generation;history.update(input,true,input.sampled_at);
			check(!history.get(rear,hi::button::secondary).press,"reconnecting while holding release does not eject");
		}
	}
}
