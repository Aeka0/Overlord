#pragma once
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/aug/profile.hpp"
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/l86/profile.hpp"
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/p90/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
#include "component/vr/gameplay/weapons/tmp/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/break_action_runtime.hpp"
#include "component/vr/gameplay/weapons/m79/profile.hpp"
#include "component/vr/gameplay/weapons/ranger/profile.hpp"
#include "component/vr/gameplay/weapons/model1887/profile.hpp"
#include "component/vr/gameplay/weapons/l86/reload_profile.hpp"

namespace ergonomics_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;namespace hi=vr::gameplay::hand_interaction;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;using namespace std::chrono_literals;
		const quat mirror{1,0,0,0};constexpr float units=39.37007874f;
		for(const auto* d:{&w::l86::physical,&w::cheytac::physical,&w::acr::physical,&w::mp5::physical,&w::ump::physical,&w::aug::physical,
			&w::tavor::physical,&w::scar::physical,&w::fal::physical,&w::tmp::physical,&w::m4::physical,&w::m16::physical})
		for(int actor=0;actor<2;++actor)for(size_t style=0;style<d->slide_grips.size();++style)
		{
			std::array<w::part_grip_pose,w::max_part_grips> poses{};
			for(size_t i=0;i<d->slide_grips.size();++i)poses[i]=actor?vr::gameplay::hands::pose_mirror::part(d->slide_grips[i],mirror):d->slide_grips[i];
			const auto& pose=poses[style];bool complete=pose.fingers.size()==18;
			const auto palm=pose.palm==w::part_palm_facing::up?1.f:-1.f;
			if(!(pose.allowed_hands&(1u<<actor)))
			{
				check(w::choose_part_grip({poses.data(),d->slide_grips.size()},pose.wrist,{},d->slide_grab_low,d->slide_grab_high,units,d->slide_capture,actor,palm).pose!=style,
					"removed hand-specific pose cannot be acquired");continue;
			}
			for(size_t i=0;i<pose.fingers.size();++i){const auto name=pose.fingers[i].name;
				complete=complete && (name.starts_with("j_index_le_") || name.starts_with("j_mid_le_") || name.starts_with("j_ring_le_") ||
					name.starts_with("j_pinky_le_") || name.starts_with("j_thumb_le_") || name=="j_ringpalm_le" || name=="j_pinkypalm_le" || name=="j_webbing_le");
				for(size_t j=0;j<i;++j)complete=complete && name!=pose.fingers[j].name;}
			check(complete,"authored operation hands have 18 unique anatomical joints; ring must not become leng");
			const auto candidate=w::choose_part_grip({poses.data(),d->slide_grips.size()},pose.wrist,{},d->slide_grab_low,d->slide_grab_high,units,d->slide_capture,actor,palm);
			const bool same_left_m200=d->interaction.manual_bolt && actor==0;
			check(candidate.distance_meters<.002f && (candidate.pose==style || (same_left_m200 && candidate.pose==0)),
				"each authored palm facing acquires the real handle and its own style; M200 left aliases one overhand");
		}
		for(const auto* d:{&w::mp5::physical,&w::ump::physical,&w::aug::physical,&w::tavor::physical,&w::scar::physical,&w::fal::physical})
		for(int actor=0;actor<2;++actor)
		{
			const auto pinky_style=actor && d==&w::scar::physical?0:1;
			const auto pose=actor?vr::gameplay::hands::pose_mirror::part(d->slide_grips[pinky_style],mirror):d->slide_grips[pinky_style];
			const auto contact=compose(pose.wrist,{pose.contact_in_wrist,{0,0,0,1}}).position;
			check(pose.fingers.data()==w::hand_poses::edge_handle::pinky_fingers.data() &&
				pose.wrist.position[1]>contact[1]+.4f,"side pinky grasp keeps the wrist outside the receiver for either hand");
		}
		check(w::hand_poses::edge_handle::source_styles[0].fingers.data()==w::hand_poses::edge_handle::index_fingers.data() &&
			w::hand_poses::edge_handle::source_styles[1].fingers.data()==w::hand_poses::edge_handle::pinky_fingers.data() &&
			w::hand_poses::edge_handle::pinky_fingers[0].rotation[0]<.6f &&
			w::hand_poses::edge_handle::pinky_fingers[6].rotation==w::hand_poses::edge_handle::fingers[6].rotation,
			"left-down/right-up pinky style redirects the index from its root and preserves natural curl");
		check(w::acr::action_grips.size()==2 && w::acr::action_grips[1].fingers.data()==w::hand_poses::edge_handle::pinky_fingers.data() &&
			w::acr::action_grips[1].allowed_hands==3 && w::acr::action_grips[1].opposite_pose,
			"ACR alternate facing is selectable with either hand and has an explicit right-hand fit");
		check(w::cheytac::action_grips[0].fingers.data()==w::cheytac::m200_left_down_fingers.data() &&
			w::cheytac::action_grips[0].opposite_pose && w::cheytac::action_grips[0].opposite_pose->fingers.data()==w::cheytac::right_up_power_fingers.data() &&
			w::cheytac::action_grips[1].fingers.data()==w::cheytac::m200_left_down_fingers.data() &&
			w::cheytac::m200_left_down_fingers.data()==w::cheytac::right_up_power_fingers.data(),
			"M200 left-down and right-up share one complete finger chain through anatomical mirroring");
		const auto& right_fit=*w::cheytac::action_grips[0].opposite_pose;
		const auto& left_fit=w::cheytac::action_grips[0];
		check(length(sub(compose(left_fit.wrist,{left_fit.contact_in_wrist,{0,0,0,1}}).position,
			compose(right_fit.wrist,{right_fit.contact_in_wrist,{0,0,0,1}}).position))<.00001f,
			"M200 flipping the complete hand preserves the real bolt contact");
		check(w::tmp::action_grips[0].opposite_pose && w::tmp::action_grips[0].opposite_pose->fingers.data()==w::tmp::right_scissor_fingers.data(),
			"TMP right hand uses the index/middle scissor with separate thumb posture");
		check(w::tavor::action_grips[0].wrist.position[0]>5.4f && w::tavor::action_grips[0].wrist.position[1]>4.f,
			"Tavor down-facing grasp advances forward and outward from the native wrist");
		for(const auto* d:{&w::p90::physical,&w::p90::arctic})for(int actor=0;actor<2;++actor)for(std::uint8_t style=0;style<2;++style)
		{
			const auto in_wrist=actor?vr::gameplay::hands::pose_mirror::object_in_wrist(d->magazine_rest,d->magazine_grasps[style].in_wrist,mirror):d->magazine_grasps[style].in_wrist;
			const auto wrist=compose(d->magazine_rest,inverse(in_wrist));
			const auto selected=w::select_magazine_grip(*d,wrist.rotation,actor,mirror,false,false,0);
			check(selected.index==style,"P90 thumb direction follows relative wrist facing for both hands and skins");
			const auto old=w::select_magazine_grip(*d,wrist.rotation,actor,mirror,false,true,0);
			const auto palm=actor?vr::gameplay::hands::pose_mirror::local_point(w::p90::magazine_palm_in_wrist,mirror):w::p90::magazine_palm_in_wrist;
			const auto contact=compose(wrist,{palm,{0,0,0,1}}).position;
			const auto previous=compose(compose(d->magazine_rest,inverse(old.in_wrist)),{palm,{0,0,0,1}}).position;
			check(std::abs(contact[0]-previous[0]+(style?5.f/2.54f:0.f))<.0001f && std::abs(contact[2]-previous[2])<.0001f &&
				(style==0 || std::abs(contact[1]+previous[1]-2*w::p90::magazine_grasp_centre_y)<.0001f),
				"P90 exchanges grasp sides and moves the forward palm exactly 5 cm rearward");
			const auto physical=w::magazine_contacts(*d,{{},{0,0,0,1}},wrist,d->magazine_rest,units,&selected.contact);
			check(physical.grip_distance<d->interaction.manual_magazine->grab_radius,"both P90 facings contact the real magazine with an anatomical point");
			Fixture f(d);f.owner.rear=vr::hand(1-actor);f.step();f.geometry.waist_distance=0;f.geometry.magazine_pose=style;f.trigger(true);
			check(f.control.magazine_pose()==style && f.state.magazine_hand==vr::hand(actor),"waist draw latches chosen magazine pose");
			f.geometry.magazine_pose=1-style;f.step();check(f.control.magazine_pose()==style,"held P90 magazine never reselects as wrist rotates");
			f.writable=false;check(!f.interrupt() && f.control.magazine_pose()==style,"failed cleanup retains escrow pose as well as ammunition");
			f.writable=true;f.interrupt();f.trigger(false);f.trigger(true);check(f.control.magazine_pose()==1-style,"new magazine acquisition may select the opposite thumb direction");
		}
		for(const auto* d:{&w::cheytac::physical,&w::cheytac::desert})for(int actor=0;actor<2;++actor)for(std::uint8_t style=0;style<2;++style)
		{
			Fixture test(d);test.owner.rear=vr::hand(1-actor);test.step();test.geometry.slide_distance=0;test.geometry.slide_pose=style;test.trigger(true);
			check(test.control.slide_held() && test.control.slide_grip().pose==style,"M200 both hands acquire and retain their explicit bolt style");
			p::presentation view;view.active=true;view.owner=test.owner;view.definition=d;view.ammo=test.state;view.slide_held=true;view.slide_grip.pose=style;
			p::scene_frame scene;scene.owner=test.owner;scene.definition=d;scene.assembly=1;scene.binding.valid=true;scene.binding.mirror=mirror;
			hi::frame f;f.valid_hands=3;f.body.units_per_meter=units;f.objects[0].owner=test.owner;f.objects[0].assembly=1;f.objects[0].gun.rotation={0,0,0,1};
			const auto grasp=actor?vr::gameplay::hands::pose_mirror::part(d->slide_grips[style],mirror):d->slide_grips[style];f.wrists[actor]=grasp.wrist;
			const auto point=scale(compose(grasp.wrist,{grasp.contact_in_wrist,{0,0,0,1}}).position,1/units);
			check(p::sample_contact(scene,view,f) && scene.contact.slide_pose==style && length(sub(scene.contact.bolt_hand,point))<.00001f,
				"manual bolt samples the selected hand contact, never hard-coded style zero");
			const auto& motion=*d->interaction.manual_bolt;const auto point_at=[&](float lift,float travel){const float angle=motion.radians*lift;
				return vec{motion.pivot[0]-travel*motion.stroke,motion.pivot[1]-.08f*std::cos(angle),motion.pivot[2]-.08f*std::sin(angle)};};
			test.trigger(false);test.geometry.bolt_hand=point_at(0,0);test.trigger(true);
			for(const auto target:{w::manual_bolt::target{1,0},{1,1},{1,0},{0,0}}){test.geometry.bolt_hand=point_at(target.lift,target.travel);test.step();}
			test.trigger(false);check(w::mechanics::ready(*test.rules,test.state) && test.spent==1,"either M200 hand completes lift/extract/feed/lock exactly once");
		}
		const auto start=p::clock::time_point{1s};w::hold owner{17,1,vr::hand::right,vr::hand::none,w::hold_source::interaction,1};
		for(const auto* d:{&w::m79::feed,&w::ranger::feed})
		{
			w::break_action::presentation v;v.active=true;v.owner=owner;v.definition=d;v.reference=3;v.sampled_at=start;v.ammo.hinge=.3f;v.ammo.phase=w::break_action::action::opening;
			const auto a=w::break_action::displayed_hinge(v,owner,3,start+5ms),b=w::break_action::displayed_hinge(v,owner,3,start+11ms);
			check(a>.3f && b>a && v.ammo.hinge==.3f,"break action advances at display cadence without mechanical writes");
			check(w::break_action::displayed_hinge(v,owner,4,start+11ms)==.3f && w::break_action::displayed_hinge(v,owner,3,start+200ms)==.3f,"recenter/stale break action cannot extrapolate");
			v.ammo.phase=w::break_action::action::closing;v.ammo.hinge=.01f;check(w::break_action::displayed_hinge(v,owner,3,start+40ms)>0,"display cannot invent a mechanically uncommitted closed endpoint");
		}
		w::tube::presentation v;v.active=true;v.owner=owner;v.definition=&w::model1887::feed;v.reference=3;v.sampled_at=start;v.lever.spinning=true;v.lever.spin=.1f;v.ammo.stored=1;
		const auto a=w::tube::displayed_lever_pose(v,owner,3,start+5ms),b=w::tube::displayed_lever_pose(v,owner,3,start+11ms);
		check(b.spin>a.spin && a.spin>v.lever.spin && v.lever.spin==.1f,"M1887 wrist and lever advance between authoritative ticks");
		v.ammo.stored=0;v.lever.spin=.19f;const auto empty=w::tube::displayed_lever_pose(v,owner,3,start+50ms);
		check(empty.spin<=.200001f && empty.open==1,"empty M1887 display stops at open, never predicts a full rotation");
		v.ammo.stored=1;v.lever.spin=w::lever::catch_progress;v.lever.open=0;
		check(w::tube::displayed_lever_pose(v,owner,3,start+50ms).spin==w::lever::catch_progress,"display waits for authoritative spin catch");
	}
}
