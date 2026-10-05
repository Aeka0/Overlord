#pragma once
#include "component/vr/gameplay/weapons/dragunov/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"

namespace weapon_polish_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;namespace m=w::mechanics;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		constexpr float units=39.37007874f;const quat mirror{1,0,0,0};
		for(const auto* d:w::reload_profiles)
		{
			const bool latch=d->native_name=="ump45" || d->native_name=="aug" || d->native_name=="tavor" ||
				d->native_name=="fn2000" || d->native_name=="famas";
			if(!latch)continue;
			check(d->interaction.manual_magazine->spare_strike && d->magazine_contacts->strike_regions.size()==1,
				"each reviewed family and skin exposes the complete magazine body");
			for(auto rear:{vr::hand::left,vr::hand::right})for(size_t end=0;end<d->magazine_contacts->strike_regions.size();++end)
			for(auto gun:{anchor{{},{0,0,0,1}},anchor{{120,-37,91},normalize({.2f,.5f,-.1f,.7f})}})
			for(int mode=0;mode<6;++mode)
			{
				Fixture f(d);f.owner.rear=rear;f.step();f.geometry.waist_distance=0;f.trigger(true);
				const auto spare=f.state.held_rounds;const auto total=m::total_rounds(f.state);const bool chamber=f.state.chamber_loaded;
				const auto direction=d->interaction.manual_magazine->latch_direction;
				const auto centre=d->magazine_contacts->strike_regions[end].pose.position;
				const auto rotation=normalize(quat{.1f,.3f,-.2f,.9f});
				const auto sample=[&](vec offset) {
					const anchor local{sub(add(d->magazine_contacts->latch,scale(offset,units)),rotate(rotation,centre)),rotation};
					f.geometry.magazine=w::magazine_contacts(*d,gun,{},compose(gun,local),units);
					// This trajectory deliberately traverses the primary latch only.
					// Its reverse may legitimately hit Tavor's opposite-facing paddle.
					f.geometry.magazine.second_strike.reset();
					check(f.geometry.magazine.valid && f.geometry.magazine.strike.has_value(),"production sampler resolves complete magazine bodies in rotated gun space");
					f.step();
				};
				if(mode==5)f.writable=false;
				const auto sweep=[&] {
					if(mode==2){sample({});sample({});return;} // Spawn and linger inside.
					if(mode==3){sample(scale(direction,-.5f));sample({});return;} // Tracking jump.
					if(mode==4){sample(scale(direction,-.45f));for(int i=0;i<=4500;++i)sample(scale(direction,-.45f+i*.0001f));return;}
					for(int i=0;i<=23;++i)sample(scale(direction,(-.46f+i*.02f)*(mode==1?-1.f:1.f)));
				};
				sweep();
				check(f.state.magazine_inserted==(mode!=0),"directed spare strike rejects reverse, overlap, jump, slow movement and failed native compare");
				check(f.state.magazine_hand==vr::hand(1-int(rear)) && f.state.held_rounds==spare &&
					f.state.chamber_loaded==chamber && m::total_rounds(f.state)+f.spent==total,
					"latch strike preserves spare ownership, chamber and ammunition accounting");
				const int commits=f.commits,attempts=f.attempts;sample({});f.step(false);
				check(f.commits==commits && f.attempts==attempts,"sustained contact and duplicate input cannot retry a latch transaction");
				if(mode==5){f.writable=true;sweep();check(!f.state.magazine_inserted,"failed native latch write retries only after a fresh separated stroke");}
			}
		}
		for(const auto* d:{&w::dragunov::physical,&w::dragunov::arctic_physical,&w::dragunov::woodland_physical,&w::m14ebr::physical,&w::m14ebr::arctic})
		for(int actor=0;actor<2;++actor)
		{
			std::array<w::part_grip_pose,2> poses{};
			for(size_t i=0;i<2;++i)poses[i]=actor?vr::gameplay::hands::pose_mirror::part(d->slide_grips[i],mirror):d->slide_grips[i];
			for(float travel:{0.f,d->interaction.slide_stroke})for(const auto& pose:poses)
			{
				const auto offset=scale(d->interaction.slide_axis,travel*units);auto wrist=pose.wrist;wrist.position=add(wrist.position,offset);
				const auto found=w::choose_part_grip(poses,wrist,offset,d->slide_grab_low,d->slide_grab_high,units,d->slide_capture,actor);
				check(found.distance_meters<d->interaction.slide_radius,"precision handles remain reachable on either hand and at both travel limits");
				const auto& box=*d->magazine_contacts;
				for(float x:{box.grab_low[0],box.grab_high[0]})for(float y:{box.grab_low[1],0.f,box.grab_high[1]})for(float z:{box.grab_low[2],box.grab_high[2]})
				{
					wrist.position={x,y,z};
					check(w::choose_part_grip(poses,wrist,offset,d->slide_grab_low,d->slide_grab_high,units,d->slide_capture,actor).pose==w::no_part_grip,
						"precision charging capture does not cover the seated magazine model even with hook-facing wrists");
				}
			}
			// Follow the same current-frame controller sampling as real acquisition.
			Fixture f(d);f.owner.rear=vr::hand(1-actor);f.step();
			p::presentation view;view.active=true;view.definition=d;view.owner=f.owner;view.ammo=f.state;
			p::scene_frame scene;scene.owner=f.owner;scene.definition=d;scene.assembly=1;scene.binding.valid=true;scene.binding.mirror=mirror;
			vr::gameplay::hand_interaction::frame frame;frame.valid_hands=3;frame.input=f.input;frame.body.units_per_meter=units;
			frame.objects[0].owner=f.owner;frame.objects[0].assembly=1;frame.objects[0].gun.rotation={0,0,0,1};
			const auto grasp=w::select_magazine_grip(*d,{0,0,0,1},actor,mirror,false,false,0,std::nullopt,true);
			const auto wrist=compose(d->magazine_rest,inverse(grasp.in_wrist));
			frame.wrists[actor]=wrist;
			check(p::sample_contact(scene,view,frame) && scene.contact.slide_distance>d->interaction.slide_radius,
				"authored seated-magazine grasp cannot be stolen by the nearby precision charging handle");
		}
	}
}
