#pragma once
#include "component/vr/gameplay/magazine_release_motion.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/falling_rail_presentation.hpp"
#include "magazine_box_tests.hpp"

namespace magazine_release_motion_tests
{
	template<class Fixture,class Check>void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;namespace motion=vr::gameplay::motion;
		using namespace vr::gameplay::hands;using namespace std::chrono_literals;
		const auto born=motion::clock::time_point{1s};
		{
			p::magazine_latch_contact detector;p::magazine_strike strike;
			auto a=magazine_box_tests::point_fixture({-.09f,-.01f,0});auto b=a;b.frame.position={.01f,.01f,0};
			detector.update(p::rocking_magazine(),a,born,.35f);
			check(detector.update(p::rocking_magazine(),b,born+20ms,.35f,&strike) && length(sub(strike.velocity,{5,1,0}))<.001f,
				"accepted contact retains the full material-point velocity, including lateral motion");
			check(!detector.update(p::rocking_magazine(),b,born+30ms,.35f,&strike) && length(strike.velocity)==0,
				"a consumed contact cannot replay its old impulse");
		}
		{
			p::magazine_latch_contacts detector;p::magazine_strike strike;p::magazine_contact contact;
			contact.valid=true;contact.strike=magazine_box_tests::point_fixture({1,1,1});
			contact.second_strike=p::magazine_contact::directed_strike{magazine_box_tests::point_fixture({.09f,0,0}),{-1,0,0}};
			detector.update(p::rocking_magazine(),contact,born,.35f);
			contact.second_strike->motion.frame.position={-.01f,0,0};
			check(detector.update(p::rocking_magazine(),contact,born+20ms,.35f,&strike) && strike.second_latch &&
				length(sub(strike.velocity,{-5,0,0}))<.001f,"second release publishes its own direction and hardware identity");
		}
		for(const auto* d:w::reload_profiles)
		{
			if(!d->interaction.manual_magazine || !d->interaction.manual_magazine->spare_strike)continue;
			const bool button=d->native_name=="tavor" || d->native_name=="fn2000" || d->native_name=="aug";
			for(float units:{20.f,39.37007874f,80.f})for(const auto rotation:{quat{0,0,0,1},normalize(quat{.2f,-.3f,.4f,.8f})})
			{
				const anchor attached{{20000,-20000,40000},multiply(rotation,d->magazine_rest.rotation)};
				const auto v=scale(d->interaction.manual_magazine->latch_direction,2.f);
				const auto impulse=w::magazine_release_impulse(*d,{v,false},attached,units);
				check(button?length(impulse.velocity)==0 && length(impulse.angular_velocity)==0:
					length(sub(impulse.velocity,scale(rotate(rotation,v),.8f*units)))<.001f && length(impulse.angular_velocity)>0,
					"registered paddles retain strike direction in world space; physical release buttons fall naturally");
				const auto fast=w::magazine_release_impulse(*d,{scale(v,100000.f),false},attached,units);
				check(length(fast.velocity)<=3.0001f*units && (button || std::abs(length(fast.velocity)-3.f*units)<.001f) &&
					length(fast.angular_velocity)<=6.001f,
					"extreme but finite strikes have bounded translation and spin");
				const auto released=w::magazine_release_flight(*d,attached,units,born,impulse);
				check(released.valid() && released.detached && released.rail_seconds==0 && length(released.velocity)==0,
					"every accepted strike starts free flight without a rail delay or inherited rail speed");
			}
		}
		for(const auto* d:w::tavor::skins)
		{
			const auto rear=w::magazine_release_impulse(*d,{{1,0,1},false},d->magazine_rest,40);
			const auto front=w::magazine_release_impulse(*d,{{-2,0,0},true},d->magazine_rest,40);
			check(length(rear.velocity)==0 && length(rear.angular_velocity)==0 && front.velocity[0]<0 && length(front.angular_velocity)>0,
				"Tavor rear button remains gravity-only while its separate forward paddle imparts motion");
		}
		for(auto rear:{vr::hand::left,vr::hand::right})for(bool writable:{false,true})
		{
			Fixture f(&w::ak47::physical);f.owner.rear=rear;f.step();f.geometry.waist_distance=0;f.trigger(true);
			f.geometry.magazine.strike=magazine_box_tests::point_fixture({-.09f,0,0});f.step();
			f.writable=writable;f.geometry.magazine.strike->frame.position={.01f,0,0};f.step();
			check(writable?f.committed_strike && f.committed_strike->velocity[0]>0:!f.committed_strike,
				"only a successful native latch transaction publishes strike motion in either hand");
			check(!f.control.release_strike(),"strike data is scoped to the synchronous transaction, including rejection");
			f.writable=true;const auto commits=f.commits;f.step();
			check(f.commits==commits,"neither accepted nor rejected impacts queue a delayed transaction");
		}
		{
			Fixture f(&w::m4::physical);f.button(true);
			check(!f.state.magazine_inserted && !f.committed_strike && !f.control.release_strike(),
				"ordinary controller-button ejection never receives a latch impulse");
		}
		const anchor start{{10,20,40},normalize({.1f,.2f,-.3f,.8f})};
		const motion::release_impulse impulse{{12,-4,2},{1,2,3},{.5f,1,-2}};
		auto falling=w::magazine_release_flight(w::ak47::physical,start,40,born,impulse);
		auto natural=w::magazine_release_flight(w::tavor::physical,start,40,born,motion::release_impulse{});
		const auto ordinary=w::magazine_release_flight(w::m4::physical,start,40,born,std::nullopt);
		check(ordinary.rail_seconds==w::m4::physical.presentation.magazine_exit_seconds && !ordinary.detached,
			"ordinary controller-button release retains its authored natural exit");
		const auto centre=[](anchor a,vec pivot){return add(a.position,rotate(a.rotation,pivot));};
		const auto same_rotation=[](quat a,quat b){return length(sub(rotate(a,{1,0,0}),rotate(b,{1,0,0})))<.00001f &&
			length(sub(rotate(a,{0,0,1}),rotate(b,{0,0,1})))<.00001f;};
		check(falling.pose(born).position==start.position && falling.pose(born).rotation==start.rotation,
			"struck magazine departs from its exact attached pose without an offset or exit animation");
		check(!same_rotation(falling.pose(born+1ms).rotation,start.rotation),
			"rotation begins in the first millisecond instead of waiting for the former rail deadline");
		for(int hz:{45,90,144})
		{
			motion::rail_presentation display;
			for(int frame=0;frame<=hz;++frame)
			{
				const float age=float(frame)/hz;
				const auto at=born+std::chrono::duration_cast<motion::clock::duration>(std::chrono::duration<float>(age));
				auto published=falling;auto moved=start;moved.position={1000,2000,3000};
				published.advance(at,&moved);
				const auto pose=falling.pose(at),updated=published.pose(at),rendered=display.pose(falling,at,&moved);
				check(length(sub(pose.position,updated.position))<.001f && same_rotation(pose.rotation,updated.rotation) &&
					length(sub(rendered.position,pose.position))<.001f && same_rotation(rendered.rotation,pose.rotation),
					"simulation and final rendering release immediately and cannot follow a later moved source gun");
				const float t=std::chrono::duration<float>(at-born).count();
				auto expected=add(centre(start,falling.impulse.pivot),scale(falling.impulse.velocity,t));
				expected[2]-=.5f*9.81f*falling.units*t*t;
				check(length(sub(centre(pose,falling.impulse.pivot),expected))<.001f,
					"body centre receives strike velocity and gravity immediately, without extra rail speed");
				const auto button=natural.pose(at),gravity=motion::free_drop(start,{},t,40);
				check(button.position==gravity.position && button.rotation==start.rotation,
					"a struck release button starts immediate gravity without impact velocity or spin");
			}
		}
		check(length(sub(falling.pose(born).position,falling.pose(born+1us).position))<.001f,
			"immediate release starts continuously at the struck magazine pose");
		for(int field=0;field<3;++field)
		{
			auto invalid=falling;
			(field==0?invalid.impulse.velocity:field==1?invalid.impulse.angular_velocity:invalid.impulse.pivot)[0]=NAN;
			check(!invalid.valid(),"non-finite release motion cannot enter the falling-item pool");
		}
	}
}
