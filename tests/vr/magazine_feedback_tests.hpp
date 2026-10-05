#pragma once
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/famas/profile.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/m82/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"

namespace magazine_feedback_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;namespace hi=vr::gameplay::hand_interaction;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		const quat mirror{1,0,0,0},basis{.70710678f,0,0,.70710678f};
		const anchor gun{{14,-9,23},normalize({.1f,.3f,-.2f,.8f})};
		unsigned expanded{};
		for(const auto* d:w::reload_profiles)if(d->ammunition.release==w::mechanics::magazine_release::button && !d->interaction.support_magazine_catch)
		{
			check(d->magazine_contacts!=nullptr,"every exposed button magazine has its own body capture volume");
			if(!d->magazine_contacts)continue;
			const auto& box=*d->magazine_contacts;
			for(float units:{20.f,39.37007874f,80.f})for(int actor=0;actor<2;++actor)
			{
				const auto grasp=w::select_magazine_grip(*d,{0,0,0,1},actor,mirror,false,false,0,std::nullopt,true);
				const auto controller=multiply(gun.rotation,normalize(quat{.2f,-.3f,.1f,.9f}));
				const auto rotation=normalize(multiply(controller,w::magazine_wrist_basis(*d,grasp,basis,false)));
				const auto centre=scale(add(box.grab_low,box.grab_high),.5f);
				for(auto point:{vec{centre[0],centre[1],box.grab_low[2]},vec{centre[0],centre[1],box.grab_high[2]}})
				{
					const auto contact=compose(gun,{point,{0,0,0,1}}).position;
					const anchor wrist{sub(contact,rotate(rotation,grasp.contact)),rotation};p::geometry g;
					w::sample_attached_magazine(*d,g,gun,wrist,controller,basis,grasp,compose(wrist,grasp.in_wrist),units,false);
					check(g.magazine.valid && g.magazine.grip_distance<.00001f,"both magazine ends acquire through real finger contact in either hand and transformed gun frame");
					const auto seated=compose(compose(gun,d->magazine_rest),inverse(grasp.in_wrist));
					if(length(sub(wrist.position,seated.position))/units>d->interaction.button_magazine_radius)++expanded;
				}
				for(float gap:{.049f,.051f})
				{
					const auto contact=compose(gun,{{box.grab_high[0]+gap*units,centre[1],centre[2]},{0,0,0,1}}).position;
					const anchor wrist{sub(contact,rotate(rotation,grasp.contact)),rotation};p::geometry g;
					w::sample_attached_magazine(*d,g,gun,wrist,controller,basis,grasp,compose(wrist,grasp.in_wrist),units,false);
					check((g.magazine.grip_distance<=p::attached_magazine_radius(d->interaction))==(gap<.05f),
						"button magazine contact uses the same bounded five-centimetre margin as physical extraction");
				}
			}
		}
		check(expanded>0,"body capture includes real approaches outside the former seated-wrist sphere");
		for(const auto* d:{&w::scar::physical,&w::ak47::physical,&w::ak47::arctic,&w::ak47::digital,&w::ak47::desert,&w::ak47::woodland,
			&w::cheytac::physical,&w::cheytac::desert,&w::m82::physical,&w::m14ebr::physical,&w::m14ebr::arctic,
			&w::fn2000::physical,&w::famas::physical,&w::famas::tape,&w::famas::woodland,
			&w::tavor::physical,&w::tavor::digital,&w::tavor::woodland,&w::mp5::physical,&w::mp5::arctic,
			&w::ump::physical,&w::ump::arctic,&w::ump::digital})
		for(int actor=0;actor<2;++actor)for(auto rotation:{quat{0,0,0,1},normalize(quat{.4f,.1f,-.2f,.7f}),normalize(quat{-.2f,.5f,.3f,.6f})})
		{
			const auto grasp=w::select_magazine_grip(*d,{0,0,0,1},actor,mirror,false,false,0);
			check(d->magazine_tracking==w::magazine_tracking_frame::controller &&
				(d!=&w::scar::physical || d->magazine_grasps[grasp.index].kind==w::magazine_grasp_kind::body_wrap),
				"SCAR defaults to wrap and SCAR/AK variants explicitly own the magazine tracking frame");
			const auto wrist_basis=w::magazine_wrist_basis(*d,grasp,basis,false);
			const auto magazine_rotation=multiply(multiply(rotation,wrist_basis),grasp.in_wrist.rotation);
			for(auto axis:{vec{1,0,0},vec{0,1,0},vec{0,0,1}})
				check(dot(rotate(magazine_rotation,axis),rotate(rotation,axis))>.99999f,"full magazine orientation follows tracked hand pitch/roll/yaw without a weapon-support bias");
			Fixture f(d);f.owner.rear=vr::hand(1-actor);f.step();
			p::presentation view;view.active=true;view.definition=d;view.owner=f.owner;view.ammo=f.state;view.ammo.magazine_hand=vr::hand(actor);view.magazine_pose=grasp.index;
			p::scene_frame scene;scene.owner=f.owner;scene.definition=d;scene.assembly=1;scene.binding={basis,mirror,true};
			hi::frame frame;frame.valid_hands=3;frame.body.units_per_meter=39.37007874f;frame.objects[0].owner=f.owner;frame.objects[0].assembly=1;frame.objects[0].gun=gun;frame.wrists[actor]={{3,4,5},rotation};
			check(p::sample_contact(scene,view,frame) && dot(rotate(scene.held_world.rotation,{0,0,1}),rotate(rotation,{0,0,1}))>.99999f,
				"current-frame simulation uses the same corrected basis as magazine presentation");
		}
		for(size_t i=0;i<3;++i)
			check(std::abs((w::m16::handle_grab_high[i]-w::m16::handle_grab_low[i])-(w::m4::handle_grab_high[i]-w::m4::handle_grab_low[i]))<.000001f,
				"M16 handle capture dimensions match M4 rather than covering the forward stem");
		for(int actor=0;actor<2;++actor)
		{
			const auto pose=actor?vr::gameplay::hands::pose_mirror::part(w::m16::handle_grips[0],mirror):w::m16::handle_grips[0];
			auto remote=pose.wrist;remote.position=sub(vec{6,0,4.4f},rotate(remote.rotation,pose.contact_in_wrist));
			check(w::choose_part_grip({&pose,1},pose.wrist,{},w::m16::handle_grab_low,w::m16::handle_grab_high,39.37007874f).distance_meters<.002f,
				"shrinking the M16 handle region preserves the actual authored finger contact");
			check(w::choose_part_grip({&pose,1},remote,{},w::m16::handle_grab_low,w::m16::handle_grab_high,39.37007874f).distance_meters>w::m16::interaction.slide_radius,
				"M16 forward receiver contact no longer attracts the hand to the charging handle");
		}
		check(w::m9::physical.interaction.well_withdraw_margin==.015f && w::m9::physical.interaction.well_release_margin==.04f,
			"withdrawal clearance shrinks independently of staged insertion retention");
	}
}
