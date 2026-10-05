#pragma once
#include "component/vr/gameplay/weapons/vector/profile.hpp"
#include "vector_release_data.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/weapons/vector/reload_profile.hpp"

namespace vector_receiver_release_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;namespace m=w::mechanics;namespace hi=vr::gameplay::hand_interaction;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		constexpr float units=39.37007874f;const vec former{6.60802060f,.88895803f,1.09648597f};
		unsigned misses{};
		for(const auto* d:w::vector::skins)for(int actor=0;actor<2;++actor)
		for(const auto gun:{anchor{{},{0,0,0,1}},anchor{{43,-18,55},normalize({.3f,-.2f,.1f,.8f})}})
		for(const auto point:vector_release_data::surface)
		{
			check(length(sub(point,d->interaction.receiver_release->centre))/units<=d->interaction.receiver_release->impact.radius,
				"Vector's complete exposed paddle fits inside the existing receiver-release tolerance");
			Fixture f(d);f.owner.rear=vr::hand(1-actor);auto s=f.state;s.chamber_loaded=false;s.magazine_rounds=0;s.action=m::action_state::locked_open;f.adopt(s);f.step();
			f.geometry.waist_distance=0;f.trigger(true);f.button(true);f.button(false);f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);
			const auto total=m::total_rounds(f.state);const int commits=f.commits;
			p::presentation view;view.active=true;view.owner=f.owner;view.definition=d;view.ammo=f.state;
			p::scene_frame scene;scene.owner=f.owner;scene.definition=d;scene.assembly=1;scene.binding.valid=true;scene.contact.catch_input.valid=true;
			scene.contact_in_wrist.fill({.4f*units,0,0});scene.contact_in_wrist[0]={};
			hi::frame frame;frame.valid_hands=3;frame.body.units_per_meter=units;frame.objects[0].owner=f.owner;frame.objects[0].assembly=1;frame.objects[0].gun=gun;
			p::receiver_paddle_slap legacy;bool old_hit=false;
			for(float distance:{.12f,.07f,.025f,-.01f})
			{
				frame.input=f.input;frame.wrists[actor]=compose(gun,{add(point,vec{0,distance*units,0}),{0,0,0,1}});
				check(p::sample_contact(scene,view,frame),"Vector paddle contact uses the current raw wrist and registered receiver target");
				auto old=scene.contact.catch_input;for(auto& v:old.slap_points)v=add(v,scale(sub(d->interaction.receiver_release->centre,former),1/units));
				old_hit|=legacy.update(d->interaction.receiver_release->impact,d->interaction.receiver_release->max_speed,old,f.input.sampled_at,d->interaction.max_contact_step);
				f.geometry=scene.contact;f.step();
			}
			check(f.commits==commits+1 && m::ready(*f.rules,f.state) && f.last_effect==m::effect::action_close,
				"either hand can release Vector's actual empty-reload follower lock across both ends and both skins");
			check(m::total_rounds(f.state)==total && f.state.magazine_inserted,"Vector side release chambers once without ejecting or creating ammunition");
			if(!old_hit)++misses;
		}
		check(misses>0,"native paddle-edge paths reproduce misses caused by the old foremost-tip target");
	}
}
