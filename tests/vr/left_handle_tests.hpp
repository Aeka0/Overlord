#pragma once
#include "component/vr/gameplay/weapons/aug/profile.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"

namespace left_handle_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		constexpr float units=39.37007874f;const quat mirror{1,0,0,0};
		for(const auto* d:{&w::mp5::physical,&w::mp5::arctic,&w::ump::physical,&w::ump::arctic,&w::ump::digital,
			&w::aug::physical,&w::aug::plain,&w::tavor::physical,&w::tavor::digital,&w::tavor::woodland,&w::scar::physical,&w::fal::physical})
		for(int actor=0;actor<2;++actor)for(std::uint8_t style=0;style<2;++style)
		{
			const auto& source=d->slide_grips[style];
			const auto grasp=actor?vr::gameplay::hands::pose_mirror::part(source,mirror):source;
			const auto contact=compose(grasp.wrist,{grasp.contact_in_wrist,{0,0,0,1}}).position;
			const auto original=compose(source.wrist,{source.contact_in_wrist,{0,0,0,1}}).position;
			check(length(sub(contact,original))<.00001f,"both hands wrap the same left handle without reflecting hardware");
			if(actor)
				check(source.opposite_pose && grasp.wrist.position[1]>contact[1]+1.f &&
					grasp.fingers.data()==(style?w::hand_poses::edge_handle::pinky_fingers.data():w::hand_poses::edge_handle::index_fingers.data()),
					"right index and pinky hooks keep the wrist outside the left receiver wall");
			for(bool caught:{false,true})
			{
				if(caught && !d->handle_catch)continue;
				for(float fraction:{0.f,.5f,1.f})
				{
					if(caught && fraction!=1.f)continue;
					const auto moved=w::handle_pose(d->slide_rest,d->handle_catch,d->interaction.slide_axis,
						d->interaction.slide_stroke*units*fraction,caught?1.f:0.f);
					const auto wrist=w::carry_with_handle(d->slide_rest,moved,grasp.wrist);
					const auto tab=w::carry_with_handle(d->slide_rest,moved,{contact,{0,0,0,1}}).position;
					check(length(sub(compose(wrist,{grasp.contact_in_wrist,{0,0,0,1}}).position,tab))<.00001f,
						"resolved hand retains tab contact across the full stroke and raised catch arc");
					if(actor)check(wrist.position[1]>tab[1],"right wrist stays outside the tab even with the handle raised");
					for(bool held:{false,true})for(const auto gun:{anchor{{},{0,0,0,1}},anchor{{50,-30,20},normalize({.2f,-.3f,.4f,.8f})}})
					{
						// Only held parts can be halfway through an ordinary stroke.
						if(!held && !caught && fraction>0)continue;
						Fixture f(d);f.owner.rear=vr::hand(1-actor);f.step();
						p::presentation view;view.active=true;view.definition=d;view.owner=f.owner;view.ammo=f.state;
						view.ammo.action=caught?w::mechanics::action_state::latched_open:w::mechanics::action_state::closed;
						view.slide_held=held;view.slide_grip.pose=style;view.slide_travel=d->interaction.slide_stroke*fraction;
						p::scene_frame scene;scene.owner=f.owner;scene.definition=d;scene.assembly=1;scene.binding.valid=true;scene.binding.mirror=mirror;
						vr::gameplay::hand_interaction::frame frame;frame.valid_hands=3;frame.input=f.input;frame.body.units_per_meter=units;
						frame.objects[0].owner=f.owner;frame.objects[0].assembly=1;frame.objects[0].gun=gun;
						const quat controller{(actor?-.70710678f:.70710678f)*(style?-1.f:1.f),0,0,.70710678f};
						frame.wrists[actor]=compose(gun,{wrist.position,controller});
						scene.binding.wrist=multiply(conjugate(controller),wrist.rotation);
						check(p::sample_contact(scene,view,frame) && scene.contact.slide_pose==style && scene.contact.slide_distance<.001f &&
							length(sub(scene.contact.bolt_hand,scale(tab,1/units)))<.00001f,
							"fresh sampling acquires either left-handle style at rest or latched, and preserves a held style through travel");
					}
				}
			}
			if(!d->handle_catch)continue;
			Fixture f(d);f.owner.rear=vr::hand(1-actor);f.step();
			const auto initial=w::mechanics::total_rounds(f.state);
			f.geometry.slide_distance=0;f.geometry.slide_pose=style;f.trigger(true);
			const auto grip=f.control.slide_grip();f.move_slide(grip,f.tuning->slide_stroke);
			f.geometry.hand_in_gun=add(f.geometry.hand_in_gun,{0,0,f.tuning->manual_catch->lift_distance});f.step();
			check(f.state.action==w::mechanics::action_state::latched_open && f.control.slide_grip().pose==style,
				"either selected hook can pull fully and lift into the catch");
			f.trigger(false);f.geometry.slide_pose=1-style;f.trigger(true);
			check(f.control.slide_held() && f.control.slide_grip().pose==1-style,"released caught handle can be regrasped using the other hook");
			f.geometry.hand_in_gun=add(f.geometry.hand_in_gun,{0,0,-f.tuning->manual_catch->lift_distance});f.step();f.trigger(false);
			check(w::mechanics::ready(*f.rules,f.state) && f.spent==1 && w::mechanics::total_rounds(f.state)+f.spent==initial,
				"lowering and releasing a regrasped catch feeds once without duplicating extraction");
		}
	}
}
