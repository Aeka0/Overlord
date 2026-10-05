#pragma once
#include "component/vr/gameplay/weapons/miniuzi/profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/native_shot_history.hpp"

namespace miniuzi_reload_tests
{
	template <typename Fixture,typename Check> void run(Check&& check)
	{
		using namespace vr::gameplay::weapons;
		namespace m=mechanics; namespace p=physical_reload;
		for (auto rear:{hand::left,hand::right})
		{
			const auto off=static_cast<hand>(1-int(rear));
			for (bool inserted:{false,true})
			{
				Fixture dry(&miniuzi::physical); dry.owner.rear=rear;
				auto empty=dry.state; empty.magazine_inserted=inserted; empty.magazine_rounds=0; empty.action=action_state::cocked_open;
				dry.adopt(empty); dry.step(); const auto before=dry.native;
				auto& trigger=dry.input.trigger[int(rear)]; trigger.down=true; ++trigger.presses; dry.step();
				check(dry.state.action==action_state::closed && dry.native==before && dry.spent==0 && dry.last_effect==m::effect::dry_fire,
					"fresh empty trigger closes Mini Uzi without ammo consumption or a native shot effect");
				const auto count=dry.commits; dry.step(false); dry.step();
				check(dry.commits==count,"duplicate frames and held trigger cannot repeat dry closure");
				auto cocked=dry.state; cocked.action=action_state::cocked_open; dry.adopt(cocked); dry.step();
				check(dry.state.action==action_state::cocked_open,"recocking while trigger stays held does not invent a new deliberate press");
				trigger.down=false; dry.step(); dry.geometry.slide_distance=.01f; dry.trigger(true);
				trigger.down=true; ++trigger.presses; dry.step();
				check(dry.control.slide_held() && dry.state.action==action_state::cocked_open,"held handle blocks empty-trigger bolt release even before full stroke");
				dry.trigger(false); dry.step();
				check(dry.state.action==action_state::cocked_open,"releasing handle cannot defer a trigger edge consumed during manipulation");
				trigger.down=false; dry.step(); dry.writable=false; trigger.down=true; ++trigger.presses; dry.step(); dry.writable=true; dry.step();
				check(dry.state.action==action_state::cocked_open,"failed native comparison cannot queue a delayed empty trigger");
				check(dry.interrupt(),"dry trigger can be interrupted"); dry.step();
				check(dry.state.action==action_state::cocked_open,"reconnection with held trigger does not release sear");
				trigger.down=false; dry.step(); trigger.down=true; ++trigger.presses; dry.step();
				check(dry.state.action==action_state::closed && dry.native==before,"release and repress after interruption closes without ammo mutation");
			}
			Fixture live_press(&miniuzi::physical); live_press.owner.rear=rear; live_press.step();
			auto& live_trigger=live_press.input.trigger[int(rear)]; live_trigger.down=true; ++live_trigger.presses;
			live_press.button(true); live_press.button(false);
			check(!live_press.state.magazine_inserted && live_press.state.action==action_state::cocked_open && live_press.spent==0,
				"same-frame live trigger and eject does not synthesize a dry shot from the post-eject state");
			for (bool cock_first:{false,true}) for (bool eject_before_cock:{false,true})
			{
				Fixture f(&miniuzi::physical); f.owner.rear=rear;
				auto empty=f.state; empty.magazine_rounds=0; empty.action=action_state::closed; f.adopt(empty); f.step();
				const int total=m::total_rounds(f.state);
				const auto cock=[&] {
					f.geometry.slide_distance=.01f; f.geometry.waist_distance=1; f.trigger(true);
					check(f.control.slide_held(),"Mini Uzi top handle acquires from a fresh pinch");
					const auto grip=f.control.slide_grip(); f.move_slide(grip,.03f); f.move_slide(grip,0); f.trigger(false);
					check(f.state.action==action_state::closed && f.spent==0,"short handle pull cannot cock a closed Mini Uzi");
					f.trigger(true); const auto full=f.control.slide_grip(); f.move_slide(full,.085f);
					check(f.state.action==action_state::held_open && !m::ready(*f.rules,f.state) && f.spent==0,
						"full handle pull holds bolt open without any extraction");
					f.move_slide(full,0); f.trigger(false);
					check(f.state.action==action_state::cocked_open && f.control.slide_travel()==0 &&
						p::retained_internal_bolt(*f.rules,f.state),"handle returns independently of the cocked internal bolt");
					f.geometry.slide_distance=1;
				};
				if (eject_before_cock) { f.button(true); f.button(false); }
				if (cock_first) cock();
				if (!eject_before_cock) { f.button(true); f.button(false); }
				f.geometry.waist_distance=.01f; f.trigger(true);
				check(f.state.held_rounds==32 && !f.state.magazine_inserted,"Mini Uzi draws its thirty-two-round spare");
				f.geometry.magazine_top_in_well={0,0,-.04f}; f.step(); f.trigger(false);
				check(f.native.loaded==32 && !f.state.chamber_loaded && m::ready(*f.rules,f.state)==cock_first,
					"shared pistol-well insertion preserves the previous cocking state");
				if (!cock_first) cock();
				check(m::ready(*f.rules,f.state) && m::total_rounds(f.state)==total,"either order is ready with all rounds conserved");
				f.geometry.slide_distance=.01f; f.trigger(true); const auto grip=f.control.slide_grip();
				for (int i=0;i<3;++i) { f.move_slide(grip,.085f); f.move_slide(grip,0); }
				check(f.native.loaded==32 && f.spent==0 && !m::ready(*f.rules,f.state,f.control.slide_held()),
					"repeated live cocking never spends ammo; even the forward-held handle blocks firing");
				f.move_slide(grip,.085f);
				check(f.interrupt() && f.state.action==action_state::cocked_open && !f.control.slide_held() &&
					m::total_rounds(f.state)==total,"tracking or weapon interruption safely completes cocking without ammo loss");
			}
			Fixture support(&miniuzi::physical); support.owner.rear=rear; support.owner.support=off; support.step();
			support.geometry.slide_distance=.01f; support.geometry.waist_distance=.01f; support.trigger(true);
			check(!support.control.slide_held() && support.state.held_rounds==0,"donor support hand cannot simultaneously draw or cock");
			support.owner.support=hand::none; support.step();
			check(!support.control.slide_held(),"leaving support while pinching requires a fresh action press");
			support.trigger(false); support.trigger(true); check(support.control.slide_held(),"fresh action pinch works after support release");

			Fixture denied(&miniuzi::physical); denied.owner.rear=rear;
			auto shut=denied.state; shut.action=action_state::closed; denied.adopt(shut); denied.step();
			denied.geometry.slide_distance=.01f; denied.trigger(true); auto grip=denied.control.slide_grip(); denied.writable=false;
			denied.move_slide(grip,.085f); denied.writable=true; denied.step(); denied.trigger(false);
			check(denied.state.action==action_state::closed && denied.spent==0,"rejected cocking commit cannot become a delayed accepted stroke");
			denied.trigger(true); grip=denied.control.slide_grip(); denied.move_slide(grip,.03f);
			check(denied.interrupt() && denied.state.action==action_state::closed,"partial-pull tracking interruption cannot cock the action");

			Fixture fire(&miniuzi::physical); fire.owner.rear=rear; fire.step(); native_shot_history history;
			const int total=m::total_rounds(fire.state);
			for (int i=0;i<32;++i)
			{
				const auto before=fire.native; const int command=100+i;
				check(history.allow(fire.state.weapon,fire.state.instance_generation,command,before.loaded,true,true),"native Mini Uzi automatic shot admission");
				const auto tx=m::plan(*fire.rules,fire.state,{m::operation::accepted_shot,fire.state.weapon,
					fire.state.instance_generation,fire.state.revision,rear,rear});
				check(tx && fire.commit(tx),"accepted automatic shot commits exactly one magazine round");
				fire.state=tx.next; history.spent(fire.state.weapon,fire.state.instance_generation,command);
				check(!history.allow(fire.state.weapon,fire.state.instance_generation,command,before.loaded,true,false) &&
					history.allow(fire.state.weapon,fire.state.instance_generation,command,before.loaded,false,false),"native prediction replay does not spend another open-bolt shot");
				fire.step();
				check(fire.control.slide_travel()==0 && p::retained_internal_bolt(*fire.rules,fire.state)==(i<31),
					"internal bolt stays ready during automatic fire and closes on the last shot while handle stays forward");
			}
			check(fire.state.action==action_state::closed && !m::ready(*fire.rules,fire.state) && !fire.state.chamber_loaded &&
				m::total_rounds(fire.state)+fire.spent==total,"full Mini Uzi magazine ends closed with exact ammunition accounting");
		}
	}
}
