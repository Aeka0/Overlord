#pragma once
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/vector/profile.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/weapons/m4/reload_profile.hpp"
#include "component/vr/gameplay/weapons/mp5/reload_profile.hpp"
#include "component/vr/gameplay/weapons/vector/reload_profile.hpp"
#include "component/vr/gameplay/weapons/m9/reload_profile.hpp"

namespace support_only_reload_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;namespace hi=vr::gameplay::hand_interaction;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		for(const auto* d:{&w::m4::physical,&w::mp5::physical,&w::vector::physical,&w::m9::physical})
		for(auto holder:{vr::hand::left,vr::hand::right})for(bool turned:{false,true})
		{
			Fixture f(d);f.owner.rear=vr::hand::none;f.owner.support=holder;++f.owner.rear_revision;
			auto empty=f.state;empty.chamber_loaded=false;empty.magazine_inserted=false;empty.magazine_rounds=0;f.adopt(empty);f.step();
			const auto total=w::mechanics::total_rounds(f.state);f.geometry.waist_distance=0;f.trigger(true);
			const auto actor=vr::hand(1-int(holder));
			check(!f.owner.can_fire() && f.state.magazine_hand==actor,"foregrip-only gun draws a real spare into the other hand without firing authority");
			p::scene_frame scene;scene.owner=f.owner;scene.assembly=17;scene.definition=d;scene.binding.valid=true;
			scene.binding.wrist=normalize({.2f,-.3f,.1f,.8f});scene.binding.mirror=normalize({.1f,.4f,-.2f,.8f});
			scene.contact.catch_input.valid=true;
			hi::frame sample;sample.input=f.input;sample.valid_hands=3;sample.body.units_per_meter=39.37007874f;
			sample.body.head_yaw_axis={vec{1,0,0},vec{0,1,0},vec{0,0,1}};sample.body.head_position={0,0,60};
			const anchor gun{turned ? vec{100,-200,80}:vec{},turned ? normalize(quat{.2f,-.4f,.1f,.8f}):quat{0,0,0,1}};
			sample.objects[0].owner=f.owner;sample.objects[0].assembly=scene.assembly;sample.objects[0].gun=gun;
			const auto selected=w::select_magazine_grip(*d,{0,0,0,1},int(actor),scene.binding.mirror,false,true,f.control.magazine_pose());
			const auto grasp=selected.in_wrist;
			const auto well=compose(gun,d->well); // Actual authored well basis, not synthetic contact distances.
			anchor magazine{{},multiply(gun.rotation,d->magazine_rest.rotation)};
			magazine.position=sub(compose(well,{{0,0,-.04f*sample.body.units_per_meter},{0,0,0,1}}).position,rotate(magazine.rotation,d->magazine_top));
			const auto anatomical=compose(magazine,inverse(grasp));
			const auto basis=w::magazine_wrist_basis(*d,selected,scene.binding.wrist,false);
			sample.wrists[int(actor)]={anatomical.position,multiply(anatomical.rotation,conjugate(basis))};
			p::presentation view;view.active=true;view.owner=f.owner;view.definition=d;view.ammo=f.state;
			check(p::sample_contact(scene,view,sample) && length(sub(scene.held_world.position,magazine.position))<.0001f &&
				scene.contact.insertion_alignment>.999f,"foregrip reload samples the held magazine and well in the same rotated receiver frame");
			f.geometry=scene.contact;f.step();
			check(f.state.magazine_inserted && w::mechanics::total_rounds(f.state)==total && f.native.loaded==d->ammunition.magazine_capacity,
				"either foregrip hand permits geometrically aligned insertion and preserves ammunition");
			f.trigger(false);check(f.state.magazine_hand==vr::hand::none,"insertion releases the magazine lease normally");
		}
	}
}
