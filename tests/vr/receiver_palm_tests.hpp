#pragma once
#include "palm_contact_tests.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"

namespace receiver_palm_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;namespace m=w::mechanics;namespace hi=vr::gameplay::hand_interaction;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		constexpr float units=39.37007874f;
		for(const auto* d:w::reload_profiles)if(d->interaction.receiver_release)
		for(int actor=0;actor<2;++actor)for(bool fist:{false,true})
		for(const auto gun:{anchor{{},{0,0,0,1}},anchor{{75,-40,36},normalize({.3f,.2f,-.5f,.8f})}})
		{
			const auto hand=palm_contact_tests::shape(actor,fist);check(hand.palm.has_value(),"native palm is available to every registered receiver-release profile");if(!hand.palm)continue;
			for(int mode=0;mode<10;++mode)
			{
				Fixture f(d);f.owner.rear=vr::hand(1-actor);auto state=f.state;state.chamber_loaded=false;
				state.magazine_rounds=d->ammunition.magazine_capacity;state.action=m::action_state::locked_open;f.adopt(state);f.step();
				if(fist){f.input.squeeze[actor]={true,true,1,1};f.trigger(true);}
				if(mode==5)f.writable=false;
				if(mode==6)f.owner.support=vr::hand(actor);
				if(mode==7){f.input.squeeze[actor].down=false;f.trigger(true);}
				if(mode==9){f.input.squeeze[actor]={true,true,1,1};f.trigger(false);f.trigger(true);}
				const auto before=m::total_rounds(f.state);const int commits=f.commits;
				p::presentation view;view.active=true;view.definition=d;view.owner=f.owner;view.ammo=f.state;
				p::scene_frame scene;scene.owner=f.owner;scene.definition=d;scene.assembly=1;scene.binding.valid=true;
				scene.contact.catch_input.valid=true;scene.contact_in_wrist=hand.joints;scene.palm_in_wrist=hand.palm;
				hi::frame frame;frame.valid_hands=3;frame.body.units_per_meter=units;
				frame.objects[0].owner=f.owner;frame.objects[0].assembly=1;frame.objects[0].gun=gun;
				const auto turn=normalize(quat{.2f,.4f,.1f,.8f});
				// Contact near the thumb-side heel, well behind the finger roots.
				const auto target=p::box_point(hand.palm->pose,{-hand.palm->half[0]*.9f,hand.palm->half[1]*.7f,0});
				const auto sample=[&](float y) {
					if(mode==9)++f.input.trigger[actor].presses;
					frame.input=f.input;
					auto centre=d->interaction.receiver_release->centre;if(mode==4)centre[0]+=.30f*units;
					const anchor raw{add(centre,sub(vec{0,y*units,0},rotate(turn,target))),turn};frame.wrists[actor]=compose(gun,raw);
					check(p::sample_contact(scene,view,frame) && scene.contact.catch_input.palm.has_value(),"current-frame sampler carries the whole raw palm into receiver space");
					f.geometry=scene.contact;if(mode==3)f.geometry.catch_input.hand_world={};f.step();
				};
				const auto sweep=[&] {
					if(mode==2){sample(.5f);sample(0);return;}
					const int frames=mode==1?600:12;
					for(int i=0;i<=frames;++i)sample((.18f-.18f*i/frames)*(mode==8?-1.f:1.f));
				};
				sweep();const bool accepted=mode==0;
				const auto context=std::string("receiver palm ")+std::string(d->id)+" actor="+std::to_string(actor)+" fist="+std::to_string(fist)+" mode="+std::to_string(mode)+" commits="+std::to_string(f.commits-commits)+" chamber="+std::to_string(f.state.chamber_loaded)+" held="+std::to_string(int(f.state.magazine_hand));
				check(mode==9 ? !f.state.chamber_loaded && f.state.action==m::action_state::locked_open : f.commits==commits+int(accepted),context.c_str());
				check(m::total_rounds(f.state)==before && f.state.magazine_inserted,"palm release preserves total ammunition and the inserted magazine");
				if(accepted)check(m::ready(*f.rules,f.state) && f.state.magazine_rounds==d->ammunition.magazine_capacity-1,"palm release chambers exactly one round for either holding hand");
				if(mode==5)
				{
					const auto attempts=f.attempts;f.writable=true;sample(0);f.step(false);
					check(f.attempts==attempts,"failed palm write cannot retry through a different palm or finger point while overlapping");
					sweep();check(f.commits==commits+1,"fresh separated palm approach retries a failed native write");
				}
			}
		}
	}
}
