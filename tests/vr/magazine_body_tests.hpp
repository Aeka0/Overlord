#pragma once
#include "component/vr/gameplay/weapon_reload_profiles.hpp"

namespace magazine_body_tests
{
	template<class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;
		using namespace vr::gameplay::hands;using namespace std::chrono_literals;
		unsigned profiles{};
		for(const auto* d:w::reload_profiles)
		{
			const auto* policy=d->interaction.manual_magazine;if(!policy || !policy->spare_strike)continue;
			++profiles;
			const auto* c=d->magazine_contacts;
			check(c && c->strike_regions.size()==1 && p::valid(c->strike_regions[0]),"every enabled magazine family/skin uses one conservative whole-body collider");
			if(!c || c->strike_regions.size()!=1)continue;
			const auto& box=c->strike_regions[0];
			for(float units:{20.f,39.37007874f,80.f})
			for(const auto gun:{anchor{{},{0,0,0,1}},anchor{{85,-24,19},normalize({.2f,-.3f,.4f,.8f})}})
			for(const vec fraction:{vec{},vec{-.95f,0,0},vec{.95f,0,0},vec{0,-.95f,0},vec{0,.95f,0},vec{0,0,-.95f},vec{0,0,.95f}})
			{
				const auto material=p::box_point(box.pose,{fraction[0]*box.half[0],fraction[1]*box.half[1],fraction[2]*box.half[2]});
				const auto rotation=normalize(quat{.3f,.1f,-.2f,.9f});
				const float start=(length(box.pose.position)+length(box.half)+length(material))/units+policy->latch_rearm_radius+.1f;
				const int steps=int(std::ceil(start/.025f));p::magazine_latch_contacts detector;unsigned hits{};
				for(int i=0;i<=steps;++i)
				{
					const auto position=sub(add(c->latch,scale(policy->latch_direction,(-start+start*i/steps)*units)),rotate(rotation,material));
					auto contact=w::magazine_contacts(*d,gun,{},w::compose_reload(gun,{position,rotation}),units);
					// Isolate the primary hardware; Tavor's second latch is covered by
					// its own independent tests, with the same complete body collider.
					contact.second_strike.reset();
					check(contact.valid && contact.strike && contact.strike->region_count==1,"whole-body sampler converts the active box count and world scale together");
					hits+=detector.update(*policy,contact,p::magazine_latch_contact::clock::time_point{1s}+i*20ms,d->interaction.max_contact_step);
				}
				check(hits==1,"centre and all six magazine sides can strike the latch once in rotated gun space");
			}
		}
		check(profiles==27,"all 27 registered strike profiles are covered; unsupported feeds do not opt in");
	}
}
