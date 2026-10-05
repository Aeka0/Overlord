#pragma once
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/aug/profile.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/m82/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/p90/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include <limits>

namespace magazine_comfort_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;namespace hi=vr::gameplay::hand_interaction;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		using kind=w::magazine_grasp_kind;
		const quat mirror{1,0,0,0};constexpr float units=39.37007874f;
		const auto yaw=[](float degrees){const float a=degrees*.00872664626f;return quat{0,0,std::sin(a),std::cos(a)};};
		const auto palm_roll=[](float degrees,int actor){const float a=(actor?90.f-degrees:degrees-90.f)*.00872664626f;return quat{std::sin(a),0,0,std::cos(a)};};
		const vr::body_pose::estimate body{true,{},{{{1,0,0},{0,1,0},{0,0,1}}}};
		for(int actor=0;actor<2;++actor)
		{
			check(w::magazine_palm_intent(yaw(0),actor)==kind::body_wrap,"neutral controller palm selects inward magazine wrap in either hand");
			for(float degrees:{0.f,45.f,90.f,135.f,180.f,189.f,190.f,350.f,359.f})
				check(w::magazine_palm_intent(palm_roll(degrees,actor),actor)==kind::body_wrap,"palm-up through palm-down/190 degrees and up seam use wrap");
			for(float degrees:{190.1f,200.f,225.f,270.f,315.f})
			{
				auto q=palm_roll(degrees,actor);
				check(w::magazine_palm_intent(q,actor)==kind::bottom_pinch,"pinch requires signed outward roll beyond 190 degrees");
				for(float& x:q)x=-x;
				check(w::magazine_palm_intent(q,actor)==kind::bottom_pinch,"quaternion sign does not change signed palm roll");
			}
			check(w::magazine_palm_intent(yaw(actor?-120.f:120.f),actor)==kind::body_wrap,"moving the hand sideways is not an outward forearm roll");
			const quat upright{0,.70710678f,0,.70710678f};
			check(w::magazine_palm_intent(upright,actor)==kind::body_wrap,"vertical pointing has a degenerate roll reference and safely wraps");
			check(w::magazine_palm_intent(std::nullopt,actor)==kind::native &&
				w::magazine_palm_intent(quat{},actor)==kind::native &&
				w::magazine_palm_intent(quat{NAN,0,0,1},actor)==kind::native,"missing/invalid tracking produces fallback intent");
		}
		check(!controller_in_body(yaw(0),{}) && !controller_in_body(quat{NAN,0,0,1},body),"invalid body/controller never becomes a valid palm reference");
		for(const auto* d:{&w::m4::physical,&w::m16::physical,&w::aug::physical,&w::aug::plain,&w::fal::physical,
			&w::acr::physical,&w::acr::black,&w::acr::digital,&w::acr::arctic})
		for(int actor=0;actor<2;++actor)for(std::uint8_t style=0;style<2;++style)
		{
			const auto facing=palm_roll(style?180.f:220.f,actor);
			const auto selected=w::select_magazine_grip(*d,yaw(37),actor,mirror,false,false,0,facing);
			check(selected.index==style && selected.fingers.data()==d->magazine_grasps[style].fingers.data(),"six families and skins select a complete magazine recipe");
			bool complete=selected.fingers.size()==18;
			for(size_t i=0;i<selected.fingers.size();++i)
			{
				float norm{};for(float v:selected.fingers[i].rotation)norm+=v*v;
				complete=complete && std::isfinite(norm) && std::abs(norm-1)<.0001f;
				for(size_t j=0;j<i;++j)complete=complete && selected.fingers[i].name!=selected.fingers[j].name;
			}
			check(complete,"each fitted hand carries 18 unique normalized joints including palm/webbing");
			if(style==1)
			{
				const auto& shared=w::hand_poses::magazine::body_wrap;quat common{};bool seen{},root_seen{};float root_flex{};
				for(size_t i=0;i<selected.fingers.size();++i)
				{
					const auto& joint=selected.fingers[i];const auto name=joint.name;
					const bool four=name.starts_with("j_index_") || name.starts_with("j_mid_") || name.starts_with("j_ring_le_") || name.starts_with("j_pinky_le_");
					if(four && name.ends_with("_0"))
					{
						const unsigned finger=name.starts_with("j_index_")?0:name.starts_with("j_mid_")?1:name.starts_with("j_ring_")?2:3;
						const auto axis=w::hand_poses::magazine::body_wrap_flex_axes[finger];
						const auto delta=normalize(multiply(conjugate(shared[i].rotation),joint.rotation));const vec v{delta[0],delta[1],delta[2]};
						const float flex=dot(v,axis);
						check(length(cross(v,axis))<.00001f && std::abs(flex)<.13f,"MCP adaptation stays on each anatomical flex axis and cannot change finger splay");
						if(root_seen)check(std::abs(flex-root_flex)<.00001f,"MCP flexion is coupled across all four fingers");
						else {root_seen=true;root_flex=flex;}
					}
					if(four && name.ends_with("_1"))
					{
						const auto delta=normalize(multiply(conjugate(shared[i].rotation),joint.rotation));
						if(seen){float agreement{};for(unsigned n=0;n<4;++n)agreement+=delta[n]*common[n];check(std::abs(agreement)>.999999f,"four PIP joints share a coupled curl rather than independent random rotations");}
						else {common=delta;seen=true;}
					}
				}
			}
			const auto held=w::select_magazine_grip(*d,yaw(-90),actor,mirror,false,true,style,palm_roll(style?220.f:0.f,actor));
			check(held.index==style && held.in_wrist.position==selected.in_wrist.position && held.fingers.data()==selected.fingers.data(),"held recipe cannot change with controller or gun rotation");
			const auto fallback=w::select_magazine_grip(*d,yaw(0),actor,mirror,false,false,255);
			check(fallback.index==1 && fallback.index==d->magazine_default_pose,"all six families default to wrap when the body estimate is unavailable");
			const auto seated_wrist=compose(d->magazine_rest,inverse(selected.in_wrist));
			if(d->interaction.manual_magazine)
				check(w::magazine_contacts(*d,{{},{0,0,0,1}},seated_wrist,d->magazine_rest,units,&selected.contact).grip_distance<.005f,
					"both authored magazine contacts lie on the gun's actual grab region");
			// Both geometry and intent follow the current raw frame. The gun and
			// authored wrist basis deliberately disagree with the controller.
			Fixture test(d);test.owner.rear=vr::hand(1-actor);test.step();
			p::presentation view;view.active=true;view.definition=d;view.owner=test.owner;view.ammo=test.state;
			p::scene_frame scene;scene.owner=test.owner;scene.definition=d;scene.assembly=1;scene.binding.valid=true;scene.binding.mirror=mirror;scene.binding.wrist=yaw(145);
			hi::frame frame;frame.valid_hands=3;frame.body.units_per_meter=units;frame.body.body=body;
			frame.objects[0].owner=test.owner;frame.objects[0].assembly=1;frame.objects[0].gun={{3,4,5},yaw(-61)};
			frame.wrists[actor]={{-2,7,-8},facing};
			check(p::sample_contact(scene,view,frame) && scene.contact.magazine_pose==style,"fresh sample selects from controller/body, never authored wrist or gun facing");
			const auto mag_basis=w::magazine_wrist_basis(*d,selected,scene.binding.wrist,false);
			const auto expected=compose(anchor{frame.wrists[actor].position,multiply(facing,mag_basis)},selected.in_wrist);
			check(length(sub(scene.held_world.position,expected.position))<.0001f,"same-frame held world pose includes the selected magazine transform");
			check(dot(rotate(scene.held_world.rotation,{0,0,1}),rotate(facing,{0,0,1}))>.99999f,
				"magazine feed axis follows the tracked hand's local up in either grasp, not world-up or the rifle grip");
			view.magazine_pose=style;view.magazine_seated=true;frame.wrists[actor].rotation=palm_roll(style?220.f:0.f,actor);
			check(p::sample_contact(scene,view,frame) && scene.contact.magazine_pose==1 && scene.contact.attached_magazine_pose==1,
				"attached rifle magazine always uses wrap, including a spare inserted with bottom pinch");
			const auto turn=yaw(71);auto rotated_body=body;
			for(auto& axis:rotated_body.yaw_axis)axis=rotate(turn,axis);
			check(w::magazine_palm_intent(controller_in_body(multiply(turn,facing),rotated_body),actor)==(style?kind::body_wrap:kind::bottom_pinch),
				"joint body/controller turning preserves palm intent");
			test.geometry.waist_distance=0;test.geometry.magazine_pose=style;test.trigger(true);
			check(test.control.magazine_pose()==style && test.state.magazine_hand==vr::hand(actor),"successful draw latches complete selected pose");
			test.geometry.magazine_pose=1-style;test.step();check(test.control.magazine_pose()==style,"subsequent candidates cannot overwrite held pose");
			test.writable=false;check(!test.interrupt() && test.control.magazine_pose()==style,"failed refund retains recipe with ammunition escrow");
			test.writable=true;test.interrupt();test.trigger(false);test.trigger(true);
			check(test.control.magazine_pose()==1-style,"new acquisition can use the opposite recipe after actual release");
			Fixture rejected(d);rejected.owner.rear=vr::hand(1-actor);rejected.step();rejected.geometry.waist_distance=0;
			rejected.geometry.magazine_pose=1;rejected.writable=false;rejected.trigger(true);
			check(rejected.state.magazine_hand==vr::hand::none && rejected.control.magazine_pose()==0,"rejected draw cannot publish a ghost pose selection");
			struct capture_case {float x,start_z,end_z,alignment;bool inserted;};
			for(const auto c:{capture_case{.053f,-.12f,-.03f,1,true},capture_case{0,-.12f,-.068f,1,true},
				capture_case{0,.12f,.068f,1,true},capture_case{.058f,-.12f,-.03f,1,false},
				capture_case{.053f,-.12f,-.03f,0,false},capture_case{.053f,-.40f,-.03f,1,false}})
			{
				Fixture capture(d);capture.owner.rear=vr::hand(1-actor);
				auto empty=capture.state;empty.chamber_loaded=false;empty.magazine_inserted=false;empty.magazine_rounds=0;capture.adopt(empty);capture.step();
				const auto total=w::mechanics::total_rounds(capture.state);
				capture.geometry.waist_distance=0;capture.geometry.magazine_pose=style;
				capture.geometry.magazine_top_in_well={c.x,0,c.start_z};capture.trigger(true);
				capture.geometry.insertion_alignment=c.alignment;capture.geometry.magazine_top_in_well={c.x,0,c.end_z};capture.step();
				check(capture.state.magazine_inserted==c.inserted && w::mechanics::total_rounds(capture.state)==total,
					"expanded box-magazine well admits its new fringe but retains outer, angle, jump and ammunition guards");
			}
		}
		check(w::m4::reload_interaction.well_radius==.045f && w::m4::reload_interaction.well_contact_depth==.06f &&
			w::mp5::physical.interaction.well_radius==w::mp5::reload_interaction.well_radius,
			"comfort well expansion is a reviewed-profile opt-in, not a shared-family baseline mutation");
		// Dedicated co-grasps bypass ordinary style data, even if a future pistol
		// happens to share the same magazine-style capabilities.
		auto knife=w::m9::physical;knife.magazine_grasps=w::m4::magazine_grasps;knife.magazine_selection=w::magazine_grasp_policy::body_palm;
		const auto paired=w::select_magazine_grip(knife,yaw(0),0,mirror,true,false,1,yaw(120));
		check(paired.in_wrist.position==knife.knife_magazine_in_wrist->position,"knife magazine recipe keeps priority over ordinary comfort alternatives");
		check(w::magazine_wrist_basis(knife,paired,yaw(37),true)==yaw(37),"knife co-grasp retains its equipment wrist frame");
		const auto p90=w::select_magazine_grip(w::p90::physical,yaw(0),0,mirror,false,true,0);
		check(w::magazine_wrist_basis(w::p90::physical,p90,yaw(37),false)==yaw(37),"P90 keeps its independently authored horizontal magazine frame");
		for(const auto* d:{&w::scar::physical,&w::m14ebr::physical,&w::m14ebr::arctic,&w::m82::physical})
		for(int actor=0;actor<2;++actor)
		{
			const auto free=w::select_magazine_grip(*d,yaw(121),actor,mirror,false,false,1);
			const auto seated=w::select_magazine_grip(*d,yaw(121),actor,mirror,false,true,0,std::nullopt,true);
			check(free.index==d->magazine_default_pose && seated.index==1 && seated.fingers.size()==18 &&
				d->magazine_grasps[seated.index].kind==kind::body_wrap,"SCAR and adopted precision rifles share their fitted wrap for free and attached magazines");
			Fixture f(d);f.owner.rear=vr::hand(1-actor);f.step();f.geometry.magazine.grip_distance=0;
			f.geometry.attached_magazine_pose=seated.index;f.trigger(true);
			check(f.control.magazine_grabbed() && f.control.magazine_pose()==1,"new wrap index belongs to the actual attached acquisition");
			const auto in_gun=compose(d->magazine_rest,inverse(seated.in_wrist));
			check(w::magazine_contacts(*d,{{},{0,0,0,1}},in_gun,d->magazine_rest,units,&seated.contact).grip_distance<.01f,
				"new wrapped index pad touches the authored magazine capture box in either hand");
		}
	}
}
