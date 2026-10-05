#pragma once
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/hand_interaction/mechanical_contacts.hpp"
#include "component/vr/gameplay/weapons/m9/reload_profile.hpp"
#include "component/vr/gameplay/weapons/magnum44/profile.hpp"
#include "component/vr/gameplay/weapons/m1014/profile.hpp"
#include "component/vr/gameplay/weapons/ranger/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/reload_profile.hpp"
#include "component/vr/gameplay/weapons/spas12/profile.hpp"
#include "component/vr/gameplay/weapons/winchester1200/profile.hpp"
#include "component/vr/gameplay/weapons/m79/profile.hpp"

namespace hand_interaction_lifecycle_tests
{
	template<class Check> void run(Check check)
	{
		namespace hi=vr::gameplay::hand_interaction;namespace w=vr::gameplay::weapons;
		for(const auto rear:{vr::hand::left,vr::hand::right})
		{
			const auto off=vr::hand(1-int(rear));
			hi::frame f;f.input.sequence=17;f.input.reference_generation=4;f.input.focused=true;
			f.input.sampled_at=hi::clock::time_point{std::chrono::seconds(1)};
			f.valid_hands=3;f.body.units_per_meter=100;f.body.head_position={0,0,170};
			f.body.head_yaw_axis={{{1,0,0},{0,1,0},{0,0,1}}};
			for(int h=0;h<2;++h)
			{
				f.input.grip[h].valid=f.input.aim[h].valid=true;
				f.input.aim[h].tracking.orientation={{{1,0,0},{0,1,0},{0,0,1}}};
				f.input.trigger[h]=f.input.squeeze[h]=f.input.secondary[h]={true,false,0,1};
			}
			f.input.trigger[int(off)]={true,true,1,1};
			w::hold owner;owner.weapon=49;owner.instance_generation=3;owner.rear=rear;owner.rear_revision=1;
			f.objects[0].owner=owner;f.objects[0].assembly=9;f.objects[0].gun={{0,0,180},{0,0,0,1}};
			f.wrists[int(rear)]={{0,0,180},{0,0,0,1}};f.wrists[int(off)]={{0,21,108},{0,0,0,1}};
			const hi::access granted{true,true,true,true,false,false};
			for(const auto* definition:{&w::ranger::feed,&w::m79::feed})
			{
				namespace b=w::break_action;
				for(const bool open:{false,true})
				{
					auto state=b::import_native(definition->ammunition,49,31,{0,20});
					if(open){state.phase=b::action::open;state.hinge=1;}
					b::geometry g;g.valid=true;g.weapon=49;g.instance_generation=31;g.reference_generation=f.input.reference_generation;g.input_sequence=f.input.sequence;
					g.waist_distance=g.barrel_distance=0;g.barrel_hand={.1f,0,0};g.shell_in_chamber.fill({0,0,1});g.alignment.fill(1);
					const bool part=b::barrel_acquirable(definition->interaction,state,g);
					check(part==open,"only an open hinge offers its barrel ahead of overlapping waist supply");
					b::controller control;auto native=b::native_ammo(state);int commits=0;
					control.update(definition->interaction,definition->ammunition,state,f.input,owner,g,true,f.input.sampled_at,
						[&](const auto& tx){if(tx.before!=native)return false;native=tx.after;++commits;return true;},{true,true,!part,true,part});
					check(open?(control.barrel_held() && state.loader_hand==vr::hand::none && commits==0):
						(!control.barrel_held() && state.loader_hand==off && state.held_rounds==1 && native.reserve==19 && commits==1),
						"overlapping hinge and waist executes exactly the role selected from current mechanics");
				}
			}
			for(const auto* definition:{&w::spas12::feed,&w::winchester1200::feed})
			{
				namespace t=w::tube;t::geometry g{true,49,32,f.input.reference_generation,f.input.sequence};
				g.waist_distance=g.rack_distance=0;g.port_delta=g.tube_delta={0,0,1};g.port_alignment=g.tube_alignment=1;
				for(const bool released:{false,true})
				{
					auto input=f.input;input.secondary[int(rear)].down=released;
					auto state=t::import_native(definition->ammunition,49,32,{1,20});
					const bool unlock=t::pump_release_held(input,owner,false);
					const bool part=t::rack_acquirable(definition->interaction,definition->ammunition,state,g,0,unlock);
					check(part==released,"locked pump yields overlapping waist supply until the rear release is held");
					t::controller control;auto native=t::native_ammo(state);int commits=0;
					control.update(definition->interaction,definition->ammunition,state,input,owner,g,true,input.sampled_at,
						[&](const auto& tx){if(tx.before!=native)return false;native=tx.after;++commits;return true;},{true,true,!part,true,part});
					check(released?(control.rack_held() && state.loader_hand==vr::hand::none && commits==0):
						(!control.rack_held() && state.loader_hand==off && state.held_rounds==1 && native.reserve==19 && commits==1),
						"overlapping pump and waist executes its chosen part or supply without falling through");
				}
				auto input=f.input;input.secondary[int(off)].down=true;
				check(!t::pump_release_held(input,owner,false),"offhand release alone cannot unlock a pump it is not holding");
				auto supported=owner;supported.support=off;input.squeeze[int(off)].down=true;input.trigger[int(off)].down=false;
				check(t::pump_supported(input,supported) && t::pump_release_held(input,supported,false),"Grip support preserves the offhand unlock button");
				auto state=t::import_native(definition->ammunition,49,32,{1,20});t::controller control;int commits=0;
				control.update(definition->interaction,definition->ammunition,state,input,supported,g,true,input.sampled_at,
					[&](const auto&){++commits;return true;},{false,false,false,false,false});
				check(control.rack_held() && state.loader_hand==vr::hand::none && commits==0,"existing Grip support still operates an unlocked pump without a new pinch grant");
			}
			{
				namespace p=w::physical_reload;using namespace vr::gameplay::hands::pose_math;
				const auto& definition=w::tavor::physical;
				w::native_ammunition::reload_snapshot observed;observed.valid=true;
				observed.ammo={true,49,30,20,0,0,owner.instance_generation};observed.base_capacity=30;
				std::copy(definition.native_name.begin(),definition.native_name.end(),observed.native_name.begin());
				const auto original=observed;
				const auto imported=p::import_native_feed(definition,owner.id(),observed,30,41);
				check(imported && imported->magazine_inserted && imported->magazine_rounds==29 && observed.ammo==original.ammo && observed.native_name==original.native_name,
					"manual-magazine candidate preview imports validated native contents without changing the source");
				check(!p::import_native_feed(definition,{49,owner.instance_generation+1},observed,30,41),"preview rejects a different native instance");
				check(!p::import_native_feed(definition,owner.id(),observed,31,41),"preview rejects modified live magazine capacity");
				observed.native_name[0]='?';check(!p::import_native_feed(definition,owner.id(),observed,30,41),"preview rejects mismatched native definition");observed=original;
				observed.base_capacity=31;check(!p::import_native_feed(definition,owner.id(),observed,30,41),"preview rejects mismatched base capacity");observed=original;
				check(!p::import_native_feed(definition,owner.id(),observed,30,0),"preview never admits generation zero");
				if(imported)
				{
					auto contacts=f;p::scene_frame scene;scene.owner=owner;scene.assembly=9;scene.definition=&definition;scene.binding.valid=true;
					const auto in_wrist=int(off)?vr::gameplay::hands::pose_mirror::object_in_wrist(definition.magazine_rest,definition.magazine_in_wrist,scene.binding.mirror):definition.magazine_in_wrist;
					contacts.wrists[int(off)]=compose(contacts.objects[0].gun,compose(definition.magazine_rest,inverse(in_wrist)));
					p::presentation preview;preview.active=true;preview.owner=owner;preview.definition=&definition;preview.ammo=*imported;
					check(p::sample_contact(scene,preview,contacts) && scene.contact.magazine.grip_distance<=definition.interaction.manual_magazine->grab_radius,
						"first manual-magazine candidate includes the native inserted magazine contact");
					const float score=std::min(scene.contact.slide_distance/definition.interaction.slide_radius,
						preview.ammo.magazine_inserted?scene.contact.magazine.grip_distance/definition.interaction.manual_magazine->grab_radius:INFINITY);
					check(score<=1,"first manual-magazine pinch receives a part candidate before domain state exists");
					const auto admitted=p::import_native_feed(definition,owner.id(),observed,30,42);
					preview.ammo=*admitted;check(p::sample_contact(scene,preview,contacts) && scene.contact.instance_generation==42,
						"actual admission replaces preview generation using the same immutable tracking frame");
					p::controller control;int commits=0;
					control.update(definition.interaction,definition.ammunition,preview.ammo,scene.input,owner,scene.contact,true,f.input.sampled_at,
						[&](const auto&){++commits;return true;},{true,true,false,true,true,false});
					check(control.magazine_grabbed() && commits==0,"first granted manual-magazine pinch acquires its grasp without an ammo write");
				}
			}
			const auto settle=[&](auto& control,auto rules,auto& state,auto& native,int& commits,auto held)
			{
				const auto before=native;const auto rounds=state.held_rounds;
				check(rounds>0,"bootstrap creates actual escrow before lifecycle settlement");
				check(!control.interrupt(rules,state,rear,[](const auto&){return false;}) && state.held_rounds==rounds && held(state)==off && native==before,
					"failed lifecycle cleanup retains original escrow and physical owner");
				const auto write=[&](const auto& tx){if(native!=tx.before)return false;native=tx.after;++commits;return true;};
				check(control.interrupt(rules,state,rear,write) && state.held_rounds==0 && held(state)==vr::hand::none && native.reserve==20,
					"retry settles exactly the original reserve escrow");
				const auto count=commits;
				check(control.interrupt(rules,state,rear,write) && commits==count,"repeated lifecycle pass cannot refund twice");
			};
			{
				namespace p=w::physical_reload;p::scene_frame scene;scene.owner=owner;scene.assembly=9;scene.definition=&w::m9::physical;scene.binding.valid=true;
				p::presentation view;check(p::sample_contact(scene,view,f) && scene.contact.instance_generation==0,"unadmitted magazine contact remains explicitly unbound");
				view.active=true;view.owner=owner;view.definition=scene.definition;view.ammo={49,23,1,false,false,0,20};
				check(w::mechanics::from_native_automatic(view.definition->ammunition,0,view.ammo) && p::sample_contact(scene,view,f) && scene.contact.instance_generation==23,
					"magazine bootstrap resamples the same input with admitted mechanical generation");
				p::controller control;auto native=w::mechanics::native_ammo(view.ammo);int commits=0;
				const auto write=[&](const auto& tx){if(native!=tx.before)return false;native=tx.after;++commits;return true;};
				control.update(view.definition->interaction,view.definition->ammunition,view.ammo,scene.input,owner,scene.contact,true,f.input.sampled_at,write,granted);
				check(commits==1 && view.ammo.magazine_hand==off,"magazine draws on first granted press without a renderer round trip");
				control.update(view.definition->interaction,view.definition->ammunition,view.ammo,scene.input,owner,scene.contact,true,f.input.sampled_at,write,granted);
				check(commits==1,"duplicate magazine input keeps escrow without another draw");
				settle(control,view.definition->ammunition,view.ammo,native,commits,[](const auto& s){return s.magazine_hand;});
			}
			{
				namespace c=w::cylinder;c::scene_frame scene;scene.owner=owner;scene.assembly=9;scene.definition=&w::magnum44::feed;scene.binding.valid=true;
				c::presentation view;check(hi::sample(scene,view,f) && scene.contact.instance_generation==0,"unadmitted cylinder contact remains explicitly unbound");
				view.active=true;view.owner=owner;view.definition=scene.definition;view.ammo=c::import_native(view.definition->ammunition,49,24,{0,20});
				check(hi::sample(scene,view,f) && scene.contact.instance_generation==24,"cylinder bootstrap uses the actual admitted generation");
				c::controller control;auto native=c::native_ammo(view.ammo);int commits=0;
				const auto write=[&](const auto& tx){if(native!=tx.before)return false;native=tx.after;++commits;return true;};
				control.update(view.definition->interaction,view.definition->ammunition,view.ammo,scene.input,owner,scene.contact,true,f.input.sampled_at,write,granted);
				check(commits==1 && view.ammo.loader_hand==off,"cylinder loader draws on the first granted press");
				control.update(view.definition->interaction,view.definition->ammunition,view.ammo,scene.input,owner,scene.contact,true,f.input.sampled_at,write,granted);
				check(commits==1,"duplicate cylinder input preserves one loader");
				settle(control,view.definition->ammunition,view.ammo,native,commits,[](const auto& s){return s.loader_hand;});
			}
			{
				namespace t=w::tube;t::scene_frame scene;scene.owner=owner;scene.assembly=9;scene.definition=&w::m1014::feed;scene.binding.valid=true;
				t::presentation view;check(hi::sample(scene,view,f) && scene.contact.instance_generation==0,"unadmitted tube contact remains explicitly unbound");
				view.active=true;view.owner=owner;view.definition=scene.definition;view.ammo=t::import_native(view.definition->ammunition,49,25,{0,20});
				check(hi::sample(scene,view,f) && scene.contact.instance_generation==25,"tube bootstrap uses the actual admitted generation");
				t::controller control;auto native=t::native_ammo(view.ammo);int commits=0;
				const auto write=[&](const auto& tx){if(native!=tx.before)return false;native=tx.after;++commits;return true;};
				control.update(view.definition->interaction,view.definition->ammunition,view.ammo,scene.input,owner,scene.contact,true,f.input.sampled_at,write,granted);
				check(commits==1 && view.ammo.loader_hand==off,"tube shell draws on the first granted press");
				control.update(view.definition->interaction,view.definition->ammunition,view.ammo,scene.input,owner,scene.contact,true,f.input.sampled_at,write,granted);
				check(commits==1,"duplicate tube input preserves one shell");
				settle(control,view.definition->ammunition,view.ammo,native,commits,[](const auto& s){return s.loader_hand;});
			}
			{
				namespace b=w::break_action;b::scene_frame scene;scene.owner=owner;scene.assembly=9;scene.definition=&w::ranger::feed;scene.binding.valid=true;
				b::presentation view;check(hi::sample(scene,view,f) && scene.contact.instance_generation==0,"unadmitted hinge contact remains explicitly unbound");
				view.active=true;view.owner=owner;view.definition=scene.definition;view.ammo=b::import_native(view.definition->ammunition,49,26,{0,20});
				check(hi::sample(scene,view,f) && scene.contact.instance_generation==26,"hinge bootstrap uses the actual admitted generation");
				b::controller control;auto native=b::native_ammo(view.ammo);int commits=0;
				const auto write=[&](const auto& tx){if(native!=tx.before)return false;native=tx.after;++commits;return true;};
				control.update(view.definition->interaction,view.definition->ammunition,view.ammo,scene.input,owner,scene.contact,true,f.input.sampled_at,write,granted);
				check(commits==1 && view.ammo.loader_hand==off,"hinge shell draws on the first granted press");
				control.update(view.definition->interaction,view.definition->ammunition,view.ammo,scene.input,owner,scene.contact,true,f.input.sampled_at,write,granted);
				check(commits==1,"duplicate hinge input preserves one shell");
				settle(control,view.definition->ammunition,view.ammo,native,commits,[](const auto& s){return s.loader_hand;});
			}
		}
	}
}
