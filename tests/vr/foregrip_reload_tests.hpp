#pragma once
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/m93r/profile.hpp"
#include "component/vr/gameplay/weapons/tmp/profile.hpp"
#include "component/vr/gameplay/weapons/vector/profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/native_shot_history.hpp"

namespace foregrip_reload_tests
{
	template <typename Fixture,typename Check>
	void run(Check&& check)
	{
		using namespace vr::gameplay::weapons;
		namespace m=mechanics; namespace p=physical_reload;
		for (const auto name:{"glock","tmp"})
		{
			const auto* definition=native_reload_profile(name,32);
			check(definition,"observed 32-round native G18/TMP can enter physical reload");
			if (!definition) continue;
			Fixture f(definition); f.step();
			check(f.native.loaded==32 && f.state.chamber_loaded && f.state.magazine_rounds==31,
				"native 32 total is partitioned without inventing an extra chamber round");
			f.button(true); f.button(false); f.geometry.waist_distance=0; f.trigger(true);
			check(!f.state.magazine_inserted && f.state.held_rounds==32,"G18/TMP eject and draw a full 32-round physical magazine");
			f.geometry.magazine_top_in_well={0,0,-.04f}; f.step(); f.trigger(false);
			check(f.state.magazine_inserted && f.state.chamber_loaded && f.native.loaded==33,
				"G18/TMP pistol-well tactical replacement reaches exactly 32+1");
		}
		for (const auto* definition:{&m93r::physical,&tmp::physical,&acr::physical,&acr::black,&acr::digital,&vector::physical,&vector::black}) for (auto rear:{hand::left,hand::right})
		{
			Fixture f(definition); f.owner.rear=rear; f.step();
			const auto off=static_cast<hand>(1-static_cast<int>(rear));
			f.owner.support=off; f.geometry.slide_distance=.01f; f.geometry.waist_distance=.01f; f.trigger(true);
			check(!f.control.slide_held() && f.state.held_rounds==0,"foregrip lease excludes handle/slide and waist magazine acquisition");
			f.owner.support=hand::none; f.step();
			check(!f.control.slide_held() && f.state.held_rounds==0,"leaving foregrip while pinching cannot create a new part grab");
			f.trigger(false); f.trigger(true);
			check(f.control.slide_held() && !p::support_available(f.input,f.owner,f.control.offhand_busy(f.state)),
				"fresh pinch acquires the action and prevents same-frame support takeover");
			const auto grip=f.control.slide_grip(); const int total=m::total_rounds(f.state);
			f.move_slide(grip,definition->interaction.slide_stroke);
			check(!m::ready(*f.rules,f.state) && f.spent==1,"full held action extracts once and blocks native firing");
			check(f.interrupt() && !f.control.slide_held() && m::total_rounds(f.state)+f.spent==total,
				"tracking/switch interruption closes the action without refunding a live extraction");
			for (bool use_button:{false,true})
			{
				Fixture empty(definition); empty.owner.rear=rear;
				auto loaded=empty.state; loaded.magazine_rounds=definition->ammunition.magazine_capacity;
				loaded.chamber_loaded=false; loaded.action=m::action_state::locked_open; empty.adopt(loaded); empty.step();
				if (use_button) { empty.button(true); empty.button(false); }
				else
				{
					empty.geometry.slide_distance=.01f; empty.trigger(true); const auto contact=empty.control.slide_grip();
					empty.move_slide(contact,definition->interaction.slide_stroke); empty.move_slide(contact,0); empty.trigger(false);
				}
				check(empty.state.chamber_loaded && empty.state.magazine_inserted && empty.native.loaded==definition->ammunition.magazine_capacity &&
					empty.spent==0,"loaded magazine in locked action feeds by either button or full manual stroke without ejecting a fictitious round");
			}
			Fixture fire(definition); fire.owner.rear=rear; fire.step(); native_shot_history history;
			const int start=m::total_rounds(fire.state); int shots{}; int last_burst{};
			while (m::ready(*fire.rules,fire.state))
			{
				last_burst=0;
				const int batch=definition==&m93r::physical ? 3 : 1;
				for (int i=0;i<batch && m::ready(*fire.rules,fire.state);++i)
				{
					const int command=100+shots, before=fire.native.loaded;
					check(history.allow(fire.state.weapon,fire.state.instance_generation,command,before,true,true),"each accepted native shot is admitted independently of trigger mode");
					const auto tx=m::plan(*fire.rules,fire.state,{m::operation::accepted_shot,fire.state.weapon,
						fire.state.instance_generation,fire.state.revision,rear,rear});
					check(tx && fire.commit(tx),"burst/automatic shot spends exactly one round");
					fire.state=tx.next; history.spent(fire.state.weapon,fire.state.instance_generation,command);
					check(!history.allow(fire.state.weapon,fire.state.instance_generation,command,before,true,false) &&
						history.allow(fire.state.weapon,fire.state.instance_generation,command,before,false,false),
						"native prediction replay cannot spend the accepted burst shot again");
					++shots; ++last_burst;
				}
				fire.step();
			}
			check(shots==definition->ammunition.magazine_capacity && fire.state.action==m::action_state::locked_open &&
				m::total_rounds(fire.state)+fire.spent==start,"native fire reaches empty lock without gaining or losing rounds");
			check(definition!=&m93r::physical || last_burst==2,"twenty-round M93R ends with two accepted rounds in its last burst");
			if (definition->bolt)
			{
				check(fire.control.slide_travel()==0 &&
					std::abs(bolt_travel(*definition->bolt,0,true)-definition->bolt->locked_m)<1e-6f,
					"empty weapon retains its independent bolt while the handle returns forward");
				fire.geometry.slide_distance=.01f; fire.trigger(true); const auto contact=fire.control.slide_grip();
				fire.move_slide(contact,definition->interaction.slide_stroke); fire.move_slide(contact,0); fire.trigger(false);
				check(fire.state.action==m::action_state::locked_open && fire.control.slide_travel()==0,
					"full handle stroke against an empty follower returns and re-locks without feeding");
			}
		}
	}
}
