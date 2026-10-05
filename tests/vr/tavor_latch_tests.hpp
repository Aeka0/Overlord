#pragma once
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/reload_profile.hpp"

namespace tavor_latch_tests
{
	template<class Check>void run(Check check)
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		using namespace std::chrono_literals;
		const auto epoch=physical_reload::magazine_latch_contact::clock::time_point{1s};
		for(const auto* p:tavor::skins)for(int latch=0;latch<2;++latch)for(size_t end=0;end<p->magazine_contacts->strike_regions.size();++end)
		for(float units:{20.f,39.37007874f,80.f})for(bool reverse:{false,true})for(bool isolated:{false,true})
		{
			const auto* c=p->magazine_contacts;
			check(c->second_latch && length(sub(c->latch,c->second_latch->position))*2.54f>12.f,
				"Tavor button and forward paddle are distinct receiver-local hardware");
			const auto point=latch?c->second_latch->position:c->latch;
			const auto direction=latch?c->second_latch->direction:p->interaction.manual_magazine->latch_direction;
			physical_reload::magazine_latch_contacts detector;
			const anchor gun{{14,-8,4},normalize({.2f,-.4f,.1f,.8f})};
			unsigned hits{};
			for(int i=0;i<=12;++i)
			{
				const float distance=(.16f-.016f*i)*(reverse?1.f:-1.f);
				const auto local=sub(add(point,scale(direction,distance*units)),c->strike_regions[end].pose.position);
				const auto magazine=compose_reload(gun,{local,{0,0,0,1}});
				auto contact=magazine_contacts(*p,gun,gun,magazine,units);
				check(contact.valid && contact.second_strike.has_value(),"both latch coordinates survive gun rotation and scale conversion");
				auto policy=*p->interaction.manual_magazine;
				if(isolated)
				{
					if(latch){contact.strike=contact.second_strike->motion;policy.latch_direction=contact.second_strike->direction;}
					contact.second_strike.reset();
				}
				hits+=detector.update(policy,contact,epoch+i*20ms,p->interaction.max_contact_step);
			}
			check(reverse?(isolated?hits==0:hits<=1):hits==1,"each isolated release rejects its reverse; a combined sweep may hit the opposite-facing latch once");
		}
	}
}
