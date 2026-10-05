#pragma once
#include "component/vr/gameplay/hand_interaction/core.hpp"
#include "component/vr/gameplay/world_interaction_policy.hpp"

template<class Check>void scripted_use_input_tests(Check check)
{
	namespace hi=vr::gameplay::hand_interaction;namespace wi=vr::gameplay::interaction;
	{
		wi::query_batch batch;wi::ray aim{{0,0,0},{1,0,0},{0,0,0},100};
		wi::target gun;gun.key={84,5};gun.weapon=84;
		batch.begin(10,2);batch.record(1,aim,gun);
		check(batch.find(1,10,2,aim) && batch.find(1,10,2,aim)->key==gun.key,
			"world pickup commits the same per-hand selection offered to arbitration");
		check(!batch.find(0,10,2,aim) && !batch.find(1,11,2,aim) && !batch.find(1,10,3,aim),
			"target query reuse cannot cross hands, input frames or tracking resets");
		auto moved=aim;moved.origin[0]=1;check(!batch.find(1,10,2,moved),"changed geometry requires fresh native admission");
		batch.record(0,aim,{});const auto empty=batch.find(0,10,2,aim);
		check(empty && !*empty,"an empty press cannot acquire a different target discovered after arbitration");
		batch.begin(11,2);check(!batch.find(1,11,2,aim),"next frame does not retain dynamic target readiness");
	}
	for(auto hand:{hi::hand::left,hi::hand::right})
	{
		const unsigned h=unsigned(hand);vr::controller_input::frame f;
		f.focused=true;f.sequence=f.reference_generation=f.continuity_generation=1;f.sampled_at=hi::clock::now();
		f.squeeze[h].active=true;f.squeeze[h].generation=1;f.squeeze[h].down=true;f.squeeze[h].presses=1;
		hi::input_history input;input.update(f,true,f.sampled_at);
		check(!input.get(hand,hi::button::grip).press,"held grip at rope readiness cannot buffer a native attachment");
		++f.sequence;f.sampled_at+=std::chrono::milliseconds(11);f.squeeze[h].down=false;++f.squeeze[h].releases;
		input.update(f,true,f.sampled_at);
		++f.sequence;f.sampled_at+=std::chrono::milliseconds(11);f.squeeze[h].down=true;++f.squeeze[h].presses;
		input.update(f,true,f.sampled_at);const auto edge=input.get(hand,hi::button::grip);
		check(edge.press && edge.down,"fresh grip starts use while firearm acquisition remains suspended");
		hi::arbiter arbiter;arbiter.begin(f.sequence,f.reference_generation);
		const hi::target target{hi::domain::world,{1766,0},0,0};
		const hi::grasp grasp{target,hi::role::world,hi::button::grip,hi::recipe::single,{}};
		arbiter.offer({hand,grasp,edge.event,20,0,1,true,true});int commits{};
		arbiter.resolve([&](const auto&){++commits;return true;});
		wi::target native;native.key={1766,0};wi::use_lease lease;
		check(commits==1 && lease.begin(int(hand),native,f.reference_generation),"script-only frame reuses the normal arbiter and exact native use lease");
		++f.sequence;f.sampled_at+=std::chrono::milliseconds(11);input.update(f,true,f.sampled_at);
		check(!input.get(hand,hi::button::grip).press && input.get(hand,hi::button::grip).down,
			"continued weapon suspension preserves the held world-use input without replaying a press");
		lease.retain(1u<<h,1u<<h,f.reference_generation,true);check(lease.active(),"normal use hold survives the next scripted frame");
		lease.retain(1u<<h,1u<<h,f.reference_generation,false);check(!lease.active(),"linking or losing native rope eligibility cancels the world-use lease");
		input.invalidate();++f.sequence;input.update(f,true,f.sampled_at);
		check(!input.get(hand,hi::button::grip).press,"pause or scene exit requires release before another use");
	}
}
