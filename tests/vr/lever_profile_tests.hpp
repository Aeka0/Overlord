#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/model1887/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/tube_presenter.hpp"
#include "model1887_data.hpp"
#include "component/vr/gameplay/weapon_carry_grip.hpp"
#include "component/vr/gameplay/hand_interaction/mechanical_contacts.hpp"

namespace lever_profile_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		const auto& p=model1887::base;rig r{};r.count=77;r.gun=68;r.parent.fill(-1);r.parent[68]=13;r.arms[0].wrist=0;r.arms[1].wrist=1;
		std::array<bone_definition,256> bones{};
		for(size_t i=0;i<p.fingers.size();++i){bones[2+i].name=p.fingers[i].name;r.parent[2+i]=0;}
		for(size_t i=0;i<model1887_data::bones.size();++i)
		{bones[68+i].name=model1887_data::bones[i].name;r.weapon_bones[68+i]=true;if(i)r.parent[68+i]=68+model1887_data::bones[i].parent;}
		for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
		const std::array<model_definition,2> models{{{"viewhands_us_army",0,68},{p.receiver,68,9}}};const auto span=std::span<const bone_definition>(bones.data(),r.count);
		check(select_profile(models,r,span).value==&p && bind_weapon_poses(r,span,p).valid,"captured H2 M1887 binds complete hand and weapon poses");
		const auto parts=tube::bind_parts(r,span,p.tube);
		check(parts.valid && parts.lever_parts[0]==69 && parts.lever_parts[2]==76,"lever and child hammer bind independently with native hierarchy");
		r.parent[76]=68;check(!tube::bind_parts(r,span,p.tube).valid && !select_profile(models,r,span).value,"wrong hammer parent rejects binding");r.parent[76]=69;
		check(p.tube->matches_native("model1887",5) && !p.tube->matches_native("model1887",6) && !p.tube->matches_native("model1887_akimbo",5),"only captured native M1887 identity/capacity admitted");
		for(auto hand:{hand::left,hand::right})
		{
			const auto begin=lever::gun_offset(model1887::lever_motion,{0,0,true},hand),end=lever::gun_offset(model1887::lever_motion,{0,1,true},hand);
			check(length(begin.position)<.0001f && length(end.position)<.0001f && std::abs(begin.rotation[3])>.9999f && std::abs(end.rotation[3])>.9999f,"spin begins and ends at exact normal grip without endpoint snap");
			float angular_path{};anchor previous=begin;
			for(int i=1;i<=350;++i)
			{
				const auto current=lever::gun_offset(model1887::lever_motion,{0,float(i)/350,true},hand);
				for(auto x:current.position)check(std::isfinite(x) && std::abs(x)<40,"spin position bounded in native gun units");
				const float d=std::abs(dot(vec{previous.rotation[0],previous.rotation[1],previous.rotation[2]},vec{current.rotation[0],current.rotation[1],current.rotation[2]})+previous.rotation[3]*current.rotation[3]);
				angular_path+=2*std::acos(std::clamp(d,0.f,1.f));previous=current;
			}
			check(angular_path>5.f,"authored relative wrist path produces substantial whole-gun rotation");
		}
		for(int i=0;i<=100;++i)
		{
			const lever::pose_state motion{float(i)/100};const auto right=lever::gun_offset(model1887::lever_motion,motion,hand::right),left=lever::gun_offset(model1887::lever_motion,motion,hand::left);
			check(length(sub(left.position,vr::gameplay::hands::pose_mirror::position(right.position)))<.0001f,"left manual gun transform mirrors motion without mirroring receiver parts");
		}
		for(float open:{0.f,.25f,.5f,.75f,1.f})
		{
			const auto offset=lever::gun_offset(model1887::lever_motion,{open},hand::right);
			const anchor base{{20,5,10},{0,0,0,1}};
			const auto gun=lever::apply_gun_pose(base,offset,p.wrists[1],&p.wrists[0]);
			const auto moving=compose(inverse(offset),p.wrists[1]);
			check(length(sub(compose(gun,moving).position,compose(base,p.wrists[1]).position))<.0001f,"supported lever preserves tracked control wrist position");
			const auto desired=sub(compose(base,p.wrists[0]).position,compose(base,p.wrists[1]).position);
			const auto actual=sub(compose(gun,p.wrists[0]).position,compose(gun,moving).position);
			check(length(cross(scale(desired,1/length(desired)),scale(actual,1/length(actual))))<.0001f,"two-hand lever re-solves direction through front contact");
		}
		for(size_t joint=0;joint<model1887::lever_motion.open_fingers.size();++joint)
		{
			const auto& source=model1887::lever_motion.open_fingers[joint];
			check(source.name.find("_ri_")!=std::string_view::npos || source.name.ends_with("_ri"),"right-hand source never accidentally includes left ring joints");
			for(float phase:{0.f,.2f,.4f,.6f,.8f,1.f})
			{
				const auto q=lever::finger_rotation(model1887::lever_motion,joint,{0,phase,true});float norm{};for(auto v:q)norm+=v*v;
				check(std::isfinite(norm) && std::abs(norm-1)<.001f,"spin finger samples remain normalized");
			}
		}

		for(auto rear:{hand::left,hand::right})
		{
			namespace hi=vr::gameplay::hand_interaction;
			hi::frame input;input.body.units_per_meter=39.37007874f;input.valid_hands=1u<<int(rear);
			input.input.sequence=input.input.reference_generation=1;
			auto& object=input.objects[0];object.owner={40,1,rear,hand::none,hold_source::interaction,1};object.assembly=9;object.gun={{3,4,5},{0,0,0,1}};
			input.wrists[1-int(rear)].position={NAN,NAN,NAN};
			tube::scene_frame scene;scene.owner=object.owner;scene.assembly=9;scene.definition=p.tube;scene.binding.valid=true;
			tube::presentation view;view.active=true;view.ammo=tube::import_native(p.tube->ammunition,40,1,{5,20});
			check(hi::sample(scene,view,input) && scene.contact.valid && std::isfinite(scene.contact.hand_in_gun[0]) && scene.contact.waist_distance==10,
				"current-frame lever admission survives missing or nonfinite opposite hand without granting shell contact");
			input.valid_hands=0;check(!hi::sample(scene,view,input),"missing operating hand rejects lever contact frame");
		}

		for(auto rear:{hand::left,hand::right})
		{
			const auto fixed=rear==hand::right?p.wrists[1]:vr::gameplay::hands::pose_mirror::wrist(p.wrists[1],{1,0,0,0});
			const auto moving=compose(inverse(lever::gun_offset(model1887::lever_motion,{.7f},rear)),fixed);
			const anchor gun{{4,8,12},{0,0,0,1}};
			const auto original=carry::choose_control_contact(gun,fixed,moving,true,compose(gun,fixed).position,39.37007874f);
			const auto mechanical=carry::choose_control_contact(gun,fixed,moving,true,compose(gun,moving).position,39.37007874f);
			check(original.valid && original.attachment==control_attachment::fixed && mechanical.valid && mechanical.attachment==control_attachment::moving,
				"opened lever and original grip resolve to separate nearest control contacts on either side");
			check(!carry::choose_control_contact(gun,fixed,moving,true,{1000,1000,1000},39.37007874f).valid,"distant hand cannot acquire a control contact");
			tube::presentation view;view.active=true;view.definition=p.tube;view.owner={40,1,rear,hand::none,hold_source::interaction,1};view.lever.open=.7f;
			auto owner=view.owner;++owner.rear_revision;owner.attachment=control_attachment::fixed;
			const auto fixed_pose=tube::held_lever_pose(view,owner);const auto fixed_offset=lever::gun_offset(model1887::lever_motion,fixed_pose,rear);
			check(!fixed_pose.grasped && fixed_pose.open==.7f && length(fixed_offset.position)<.0001f && std::abs(fixed_offset.rotation[3])>.9999f,
				"same-frame fixed regrasp keeps action angle but restores ordinary gun attachment before renderer acknowledgment");
			owner.attachment=control_attachment::moving;const auto lever_pose=tube::held_lever_pose(view,owner);
			check(lever_pose.grasped && lever_pose.open==.7f,"same-frame moving regrasp uses retained lever pose");
			carry::inventory inventory;const carry::owned_weapon item{40,{false,false}};
			check(inventory.reconcile({&item,1}) && inventory.equip_definition(40,rear),"control reacquisition inventory fixture admitted");
			const auto id=inventory.find_definition(40)->id;const auto other=hand(1-int(rear));inventory.support(id,other);
			check(inventory.release(id,1u<<int(rear),carry::location::absent,true,[](const auto&){return false;}).action==carry::outcome::carry_only,"Grip release keeps supported gun with no synthetic control owner");
			const auto claim = carry::claim_grip(inventory, {
				.actor = rear,
				.pressed = true,
				.available = true,
				.control = id,
				.attachment = control_attachment::moving
			});
			check(claim.pose_changed && inventory.in_hand(rear)->owner.attachment==control_attachment::moving && inventory.invariant(),"accepted moving contact is recorded with the new carry owner revision");
			inventory.release(id,1u<<int(rear),carry::location::absent,true,[](const auto&){return false;});
			carry::claim_grip(inventory, {
				.actor = rear,
				.pressed = true,
				.available = true,
				.control = id,
				.attachment = control_attachment::fixed
			});
			check(inventory.in_hand(rear)->owner.attachment==control_attachment::fixed && inventory.invariant(),"regrasp can return to original grip without inheriting stale moving-contact ownership");
		}

		for(auto former:{hand::left,hand::right})
		{
			namespace hi=vr::gameplay::hand_interaction;
			const auto holder=hand(1-int(former));hi::frame input;input.valid_hands=3;input.body.units_per_meter=39.37007874f;
			input.input.sequence=input.input.reference_generation=1;
			tube::scene_frame scene;scene.definition=p.tube;scene.assembly=19;scene.owner={40,1,former,holder,hold_source::interaction,1};scene.binding.valid=true;
			scene.has_shell_bindings=true;scene.shell_bindings[0]={normalize(quat{.1f,.2f,.3f,1}),{1,0,0,0},true};scene.shell_bindings[1]={normalize(quat{-.2f,.1f,.4f,1}),{1,0,0,0},true};
			scene.binding=scene.shell_bindings[int(holder)]; // Last rendered free hand, now holding the fore-end.
			auto& object=input.objects[0];object.owner=scene.owner;object.owner.rear=hand::none;++object.owner.rear_revision;object.assembly=19;object.gun={{4,5,6},normalize(quat{.1f,.3f,-.2f,1})};
			input.wrists[int(former)]={{10,15,20},normalize(quat{.2f,-.2f,.1f,1})};
			const auto& binding=scene.shell_bindings[int(former)];
			const auto grasp=former==hand::right?vr::gameplay::hands::pose_mirror::object_in_wrist({},p.tube->shell_in_wrist,binding.mirror):p.tube->shell_in_wrist;
			const auto expected=compose({input.wrists[int(former)].position,multiply(input.wrists[int(former)].rotation,binding.wrist)},grasp);
			tube::presentation view;view.active=true;view.ammo=tube::import_native(p.tube->ammunition,40,1,{5,20});
			check(hi::sample(scene,view,input) && scene.owner.holding_hand()==holder && length(sub(scene.shell_world.position,expected.position))<.0001f,
				"same-frame main release selects the former lever hand's anatomical shell binding before any render refresh");
		}

	}
}
