#pragma once
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/pp2000/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/charging_handle_fold_motion.hpp"
#include "component/vr/gameplay/weapons/acr/poses.hpp"

namespace handle_refinement_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;namespace hi=vr::gameplay::hand_interaction;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;using namespace std::chrono_literals;
		const quat mirror{1,0,0,0};constexpr float units=39.37007874f;
		for(const auto* d:w::ak47::skins)for(int h=0;h<2;++h)for(size_t style=0;style<d->slide_grips.size();++style)
		{
			std::array<w::part_grip_pose,2> poses{};for(size_t i=0;i<2;++i)poses[i]=h?vr::gameplay::hands::pose_mirror::part(d->slide_grips[i],mirror):d->slide_grips[i];
			const auto expected=poses[style].wrist;
			check(w::choose_part_grip(poses,expected,{},d->slide_grab_low,d->slide_grab_high,units,d->slide_capture,h).distance_meters<d->interaction.slide_radius,
				"AK right-side authored grasps remain reachable with either hand");
			for(float y:{0.f,2.f,d->slide_capture->maximum+.01f})
			{
				auto wrong=expected;wrong.position[1]=y;
				check(w::choose_part_grip(poses,wrong,{},d->slide_grab_low,d->slide_grab_high,units,d->slide_capture,h).pose==w::no_part_grip,
					"AK assistance cannot acquire from the magazine/left side of the receiver wall");
			}
		}
		for(int h=0;h<2;++h)
		{
			const auto& d=w::scar::physical;std::array<w::part_grip_pose,2> poses{};
			for(size_t i=0;i<2;++i)poses[i]=h?vr::gameplay::hands::pose_mirror::part(d.slide_grips[i],mirror):d.slide_grips[i];
			if(h)
			{
				for(size_t i=0;i<2;++i)
				{
					const auto controller=multiply(poses[i].wrist.rotation,conjugate(w::scar::wrists[h].rotation));
					const float palm=rotate(controller,controller_palm_axis(h))[2];
					check((i?palm>.17364818f:palm<-.17364818f) &&
						w::choose_part_grip(poses,poses[i].wrist,{},d.slide_grab_low,d.slide_grab_high,units,nullptr,h,palm).pose==i,
						"SCAR right authored hook agrees with its actual controller palm and native free-wrist basis");
				}
			}
			check(w::choose_part_grip(poses,poses[1].wrist,{},d.slide_grab_low,d.slide_grab_high,units,nullptr,h,-1.f).pose==0,
				"SCAR raw palm-down rejects the up pose even when the visual wrist matches it");
			check(w::choose_part_grip(poses,poses[0].wrist,{},d.slide_grab_low,d.slide_grab_high,units,nullptr,h,1.f).pose==1,
				"SCAR raw palm-up selects the up pose independently of animation offsets");
			check(w::choose_part_grip(poses,poses[1].wrist,{},d.slide_grab_low,d.slide_grab_high,units,nullptr,h).pose==w::no_part_grip,
				"SCAR acquisition requires a real controller palm witness");
			Fixture f(&d);f.owner.rear=vr::hand(1-h);f.step();
			p::presentation view;view.active=true;view.definition=&d;view.owner=f.owner;view.ammo=f.state;
			p::scene_frame scene;scene.owner=f.owner;scene.definition=&d;scene.assembly=1;scene.binding.valid=true;scene.binding.mirror=mirror;
			hi::frame frame;frame.valid_hands=3;frame.input=f.input;frame.body.units_per_meter=units;
			frame.objects[0].owner=f.owner;frame.objects[0].assembly=1;frame.objects[0].gun.rotation={0,0,0,1};
			const quat down{h?-.70710678f:.70710678f,0,0,.70710678f};
			frame.wrists[h]={poses[0].wrist.position,down};scene.binding.wrist=multiply(conjugate(down),poses[1].wrist.rotation);
			check(p::sample_contact(scene,view,frame) && scene.contact.slide_pose==0,"SCAR current-frame sampling uses controller palm before authored wrist basis");
			view.slide_held=true;view.slide_grip.pose=1;
			check(p::sample_contact(scene,view,frame) && scene.contact.slide_pose==1,"SCAR retained up grasp is not reselected during a held stroke");
		}
		for(const auto* d:w::acr::skins)
		{
			const auto& underhand=d->slide_grips[0];
			const auto underhand_controller=multiply(underhand.wrist.rotation,conjugate(w::acr::wrists[0].rotation));
			const auto hook_contact=compose(underhand.wrist,{underhand.contact_in_wrist,{0,0,0,1}}).position;
			check(rotate(underhand_controller,controller_palm_axis(0))[2]>0 && underhand.wrist.position[1]<hook_contact[1] &&
				underhand.fingers.data()==w::acr::underhand_fingers.data(),
				"ACR selected left index pose actually presents palm-up outside the receiver with its aligned hook chain");
			check(underhand.opposite_pose && underhand.opposite_pose->wrist.position==w::acr::right_index_fit.wrist.position &&
				underhand.opposite_pose->fingers.data()==w::acr::handle_pose_fingers_0.data(),"restoring left underhand does not alter the accepted right-hand fit");
			check(w::choose_part_grip(d->slide_grips,d->slide_grips[0].wrist,{},d->slide_grab_low,d->slide_grab_high,units,nullptr,0,-1.f).pose==1,
				"ACR left palm-down cannot select the receiver-penetrating native pose");
			check(w::choose_part_grip(d->slide_grips,d->slide_grips[1].wrist,{},d->slide_grab_low,d->slide_grab_high,units,nullptr,0,1.f).pose==0,
				"ACR left palm-up retains the accepted index/middle pose even if the assisted facing prefers pinky");
			for(size_t i=0;i<2;++i)
				check(w::choose_part_grip(d->slide_grips,d->slide_grips[i].wrist,{},d->slide_grab_low,d->slide_grab_high,units,nullptr,1).pose==i,
					"ACR palm gating leaves right-hand wrist-based selection intact");
			Fixture f(d);f.step();
			p::presentation view;view.active=true;view.definition=d;view.owner=f.owner;view.ammo=f.state;
			p::scene_frame scene;scene.owner=f.owner;scene.definition=d;scene.assembly=1;scene.binding={w::acr::wrists[0].rotation,mirror,true};
			hi::frame frame;frame.valid_hands=3;frame.input=f.input;frame.body.units_per_meter=units;
			frame.objects[0].owner=f.owner;frame.objects[0].assembly=1;frame.objects[0].gun.rotation={0,0,0,1};
			const auto& pinky=d->slide_grips[1];frame.wrists[0]={pinky.wrist.position,multiply(pinky.wrist.rotation,conjugate(scene.binding.wrist))};
			check(rotate(frame.wrists[0].rotation,controller_palm_axis(0))[2]<0 && p::sample_contact(scene,view,frame) &&
				scene.contact.slide_pose==1 && scene.contact.slide_distance<d->interaction.slide_radius &&
				pinky.fingers.data()==w::hand_poses::edge_handle::pinky_fingers.data(),
				"all ACR skins retain the actual left palm-down pinky grasp through controller-to-wrist sampling");
			f.geometry=scene.contact;f.trigger(true);
			check(f.control.slide_held() && f.control.slide_grip().pose==1,"ACR left pinky sample acquires the accepted pose in simulation");
			f.trigger(false);frame.input=f.input;view.ammo=f.state;
			frame.wrists[0]={d->slide_grips[0].wrist.position,{-.70710678f,0,0,.70710678f}};
			check(p::sample_contact(scene,view,frame) && scene.contact.slide_pose==0 && scene.contact.slide_distance<d->interaction.slide_radius,
				"ACR real palm-up controller and ordinary wrist basis reach the restored index/middle grasp");
			f.geometry=scene.contact;f.trigger(true);
			check(f.control.slide_held() && f.control.slide_grip().pose==0,"ACR left palm-up index/middle acquires after releasing the pinky grasp");
		}
		for(int hand=0;hand<2;++hand)for(int axis=0;axis<3;++axis)for(float direction:{-1.f,1.f})
		{
			const auto& d=w::fn2000::physical;const auto pose=hand?vr::gameplay::hands::pose_mirror::part(d.slide_grips[0],mirror):d.slide_grips[0];
			const auto original=compose(pose.wrist,{pose.contact_in_wrist,{0,0,0,1}}).position;
			const float extra=(axis==1 && direction>0)||(axis==2 && direction<0)?.01f:0.f;
			for(float margin:{.029f+extra,.031f+extra})
			{
				auto point=original;
				for(int a=0;a<3;++a)point[a]=std::clamp(point[a],d.slide_grab_low[a],d.slide_grab_high[a]);
				point[axis]=(direction<0?w::fn2000::action_grab_low[axis]:w::fn2000::action_grab_high[axis])+direction*(w::part_grip_capture::radius_m+margin)*units;
				auto wrist=pose.wrist;wrist.position=sub(point,rotate(wrist.rotation,pose.contact_in_wrist));
				const auto candidate=w::choose_part_grip({&pose,1},wrist,{},d.slide_grab_low,d.slide_grab_high,units,nullptr,hand,-1.f);
				Fixture f(&d);f.owner.rear=vr::hand(1-hand);f.step();f.geometry.slide_distance=candidate.distance_meters;f.geometry.slide_pose=0;f.trigger(true);
				check(f.control.slide_held()==(margin<.03f+extra),"F2000 adds 1 cm only on outward/downward faces on top of the 3 cm expansion, for both hands");
			}
		}
		{
			const auto& d=w::fn2000::physical;
			check(d.slide_grips.size()==2 && d.interaction.slide_pose_count==2,"F2000 registers both palm facings with the gesture controller");
			const vec folded{5.92901087f,1.37421296f,4.03117157f};
			const auto deployed=w::carry_with_handle(compose(d.slide_rest,d.handle_fold->pivot),
				w::folded_handle_pose(d.slide_rest,*d.handle_fold,1,0),{folded,{0,0,0,1}}).position;
			check(d.slide_grips[0].wrist.position[1]<4.5f && d.slide_grips[0].fingers.data()==w::fn2000::action_fingers.data(),
				"F2000 native left index/middle fit moves inward without replacing its finger chains");
			for(int actor=0;actor<2;++actor)for(std::uint8_t style=0;style<2;++style)
			{
				std::array<w::part_grip_pose,2> poses{};
				for(size_t i=0;i<2;++i)poses[i]=actor?vr::gameplay::hands::pose_mirror::part(d.slide_grips[i],mirror):d.slide_grips[i];
				const auto& pose=poses[style];
				check(length(sub(compose(pose.wrist,{pose.contact_in_wrist,{0,0,0,1}}).position,folded))<.00001f,
					"all four deployed F2000 fits preserve the same folded acquisition point");
				check(pose.wrist.position[0]<deployed[0] && pose.wrist.position[1]>deployed[1]+.5f && pose.fingers.size()==18,
					"both F2000 hands approach from the rear outside the receiver with complete fingers");
				check(w::choose_part_grip(poses,pose.wrist,{},d.slide_grab_low,d.slide_grab_high,units,nullptr,actor).pose==w::no_part_grip,
					"F2000 new grasp requires a real controller palm witness");
				Fixture test(&d);test.owner.rear=vr::hand(1-actor);test.step();
				p::presentation view;view.active=true;view.owner=test.owner;view.definition=&d;view.ammo=test.state;
				p::scene_frame scene;scene.owner=test.owner;scene.definition=&d;scene.assembly=1;scene.binding.valid=true;scene.binding.mirror=mirror;
				hi::frame frame;frame.valid_hands=3;frame.input=test.input;frame.body.units_per_meter=units;
				frame.objects[0].owner=test.owner;frame.objects[0].assembly=1;
				for(const auto gun:{anchor{{},{0,0,0,1}},anchor{{30,-10,7},normalize({.3f,.1f,-.2f,.8f})}})
				for(float fraction:{0.f,.5f,1.f})
				{
					view.slide_held=fraction>0;view.slide_grip.pose=style;view.slide_travel=d.interaction.slide_stroke*fraction;
					const auto offset=scale(d.interaction.slide_axis,view.slide_travel*units);
					// Turn the raw palm away while held: it must not switch styles.
					const bool up=view.slide_held?!style:bool(style);
					const quat raw{(actor?-.70710678f:.70710678f)*(up?-1.f:1.f),0,0,.70710678f};
					frame.objects[0].gun=gun;frame.wrists[actor]=compose(gun,{add(pose.wrist.position,offset),raw});
					scene.binding.wrist=multiply(conjugate(raw),pose.wrist.rotation);
					check(p::sample_contact(scene,view,frame) && scene.contact.slide_pose==style && scene.contact.slide_distance<.001f &&
						length(sub(scene.contact.bolt_hand,scale(add(folded,offset),1/units)))<.00001f,
						"F2000 raw palm selects either facing and retains it over full travel in a rotated gun");
				}
				test.geometry.slide_distance=0;test.geometry.slide_pose=style;test.trigger(true);
				check(test.control.slide_held() && test.control.slide_grip().pose==style,"both F2000 styles acquire in the mechanical controller");
				test.geometry.slide_pose=1-style;test.step();
				check(test.control.slide_grip().pose==style,"held F2000 pose is not overwritten by a changed candidate");
				test.trigger(false);test.trigger(true);
				check(test.control.slide_grip().pose==1-style,"released F2000 handle can acquire the other palm facing");
			}
		}
		const auto start=p::clock::time_point{1s};p::charging_handle_fold_motion motion;
		check(motion.update(1,1,true,0,start,.075f)==1 && motion.hand()==0,"folding end opens on the acquiring hand's side");
		motion.update(1,1,true,1,start+10ms,.075f);check(motion.hand()==0,"fold side stays latched while held");
		check(motion.update(1,1,false,1,start+20ms,.075f)==1 && motion.hand()==0,"release starts folding back from the held side");
		const auto amount=motion.update(1,1,false,1,start+50ms,.075f);
		check(amount>0 && amount<1 && motion.update(1,1,false,1,start+100ms,.075f)==0,"fold return is finite and does not gate mechanical travel");
		motion.update(1,1,true,1,start+110ms,.075f);check(motion.hand()==1,"fresh opposite-hand acquisition unfolds to the other side");
		check(motion.update(2,1,false,0,start+120ms,.075f)==0 && motion.update(2,2,false,0,start+130ms,.075f)==0,"new instance or tracking reference clears folding motion");
		motion.update(2,2,true,0,start+140ms,.075f,1);motion.update(2,2,true,1,start+150ms,.075f,2);
		check(motion.hand()==1,"ownership transfer starts a new side even without an intervening rendered release");
		const auto& pp=*w::pp2000::physical.handle_fold;const anchor parent{{10,2,3},{0,0,0,1}};
		for(const auto* d:{&w::fn2000::physical,&w::pp2000::physical})for(int h=0;h<2;++h)
		{
			const auto pose=h?vr::gameplay::hands::pose_mirror::part(d->slide_grips[0],mirror):d->slide_grips[0];
			check(w::choose_part_grip({&pose,1},pose.wrist,{},d->slide_grab_low,d->slide_grab_high,units,nullptr,h,-1.f).distance_meters<.001f,
				"deployed folding grips acquire through the corresponding folded material point with either hand");
		}
		for(int h=0;h<2;++h)
		{
			const auto opened=w::folded_handle_pose(parent,pp,1,h),closed=w::folded_handle_pose(parent,pp,0,h);
			const auto point=compose(opened,{{2,0,0},{0,0,0,1}}).position;
			check((point[1]-parent.position[1])*(h?-1.f:1.f)>1.9f && length(sub(closed.position,parent.position))<.0001f && closed.rotation==parent.rotation,
				"PP2000 end opens left/right and returns straight about its own pivot");
			check(pp.end_bone=="j_reload_end" && p::native_action_recoil(w::pp2000::physical.interaction),"PP2000 end folding leaves long rod recoil intact");
			const auto& fn=*w::fn2000::physical.handle_fold;const auto end=w::folded_handle_pose(parent,fn,1,h);
			check(fn.mesh && fn.end_bone.empty() && length(sub(end.position,compose(parent,fn.pivot).position))<.0001f,
				"F2000 synthetic end pivot is independent of its axial carrier");
		}
		check(w::folded_handle_pose(parent,pp,NAN,0).position==parent.position && w::folded_handle_rotation(parent.rotation,&pp,1,9)==parent.rotation,
			"invalid folding progress or hand cannot create a non-finite or wrong-side transform");
		using scene_models::surface_face_range;
		const std::array<unsigned,3> counts{6,4,8};const std::array<surface_face_range,2> partition{{{0,1,3},{2,5,7}}};
		check(scene_models::valid_face_partition(partition,counts) && scene_models::face_in_partition(partition,0,2) &&
			!scene_models::face_in_partition(partition,1,2) && !scene_models::face_in_partition(partition,2,2),"multi-material partition keeps surface identity and face ranges");
		auto bad=partition;bad[1]={0,3,4};check(!scene_models::valid_face_partition(bad,counts),"overlapping partition ranges fail before publication");
		bad=partition;bad[1]={3,0,1};check(!scene_models::valid_face_partition(bad,counts),"foreign material surface cannot enter a partition");
		bad=partition;bad[1]={2,5,8};check(!scene_models::valid_face_partition(bad,counts),"out-of-bounds tip faces cannot truncate a receiver");
		std::array<unsigned,9> fn_counts{};for(size_t i=0;i<9;++i)fn_counts[i]=w::fn2000::handle_surfaces[i][1];
		unsigned faces{};for(auto r:w::fn2000::handle_tip_faces)faces+=r.last-r.first+1;
		check(faces==1126 && scene_models::valid_face_partition(w::fn2000::handle_tip_faces,fn_counts),"F2000 end owns exactly its 1126 faces in the original material layout");
	}
}
