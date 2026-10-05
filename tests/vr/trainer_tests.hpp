#pragma once
#include "component/vr/gameplay/trainer_policy.hpp"

namespace trainer_tests
{
	template<class Check> void run(Check& check)
	{
		using namespace vr::gameplay;
		using namespace weapons;
		using namespace weapons::carry;
		{
			// The script is blocked inside _id_CED3, so no further weapon query
			// occurs until its did_action_* notification resumes that helper.
			const auto primary=trainer::switch_event("did_action_primary");
			check(!trainer::completes_hint(primary,2,2) && !trainer::completes_hint(primary,0,2),
				"a waiting rifle hint cannot complete with the pistol or empty hands");
			check(trainer::completes_hint(primary,1,2),"a back-slot rifle draw releases the nested primary wait before the next weapon poll");
			check(trainer::completes_hint(trainer::switch_event("did_action_sidearm"),2,2) &&
				!trainer::completes_hint(trainer::switch_event("did_action_sidearm"),1,2),"the next sidearm hint waits for the actual pistol");
			check(!trainer::completes_hint(trainer::switch_event("did_action_reload"),1,2) &&
				!trainer::completes_hint(primary,1,0),"other hints and missing native pistol evidence cannot advance teaching");
		}
		for (const auto pistol_hand:{hand::left,hand::right})
		{
			const auto rifle_hand=pistol_hand==hand::left?hand::right:hand::left;
			inventory state;const std::array owned{owned_weapon{1,{}},owned_weapon{2,{true}}};
			check(state.reconcile(owned) && state.equip_definition(1,rifle_hand),"trainer starts with native rifle");
			auto selected=trainer::tutorial_weapon(state.instances(),1);
			check(selected==1,"rifle is the initial teaching weapon");
			check(state.equip_definition(2,pistol_hand),"either hip draw can hold the pistol alongside the rifle");
			selected=trainer::tutorial_weapon(state.instances(),selected);
			check(selected==2 && state.in_hand(rifle_hand)->id.weapon==1,"second-hand pistol satisfies Dunn without displacing rifle");
			const auto pistol=state.in_hand(pistol_hand)->id;
			state.release(pistol,1u<<unsigned(pistol_hand),location::right_waist,true,[](const auto&){return false;});
			selected=trainer::tutorial_weapon(state.instances(),selected);
			check(selected==1,"returning pistol while holding rifle returns the lesson to rifle");
			state.equip(pistol,pistol_hand);
			selected=trainer::tutorial_weapon(state.instances(),selected);
			state.put_away();
			check(trainer::tutorial_weapon(state.instances(),selected)==2,"empty hands cannot pass the not-pistol comparison");
			state.equip_definition(1,rifle_hand);
			check(trainer::tutorial_weapon(state.instances(),selected)==1,"redrawing rifle advances after both guns were stowed");
			const auto rifle=state.in_hand(rifle_hand)->id;
			state.support(rifle,pistol_hand);
			const auto before=state.find(rifle)->owner.rear_revision;
			state.release(rifle,1u<<unsigned(pistol_hand),location::absent,false,[](const auto&){return false;});
			check(state.find(rifle)->owner.rear_revision==before,"support changes do not invent a new firing-hand draw");
		}
		for (const auto label:{"target_enemy","target_friendly"})
		{
			check(trainer::knife_target("trainer","script_model",label,true,melee::tool::knife),"enemy and civilian targets retain native melee scoring");
			check(!trainer::knife_target("trainer","script_model",label,false,melee::tool::knife),"lowered damage-disabled targets reject repeat contacts");
			for (const auto kind:{melee::tool::fist,melee::tool::firearm,melee::tool::shield})
				check(!trainer::knife_target("trainer","script_model",label,true,kind),"the knife lesson cannot be passed with blunt physical strikes");
		}
		check(!trainer::knife_target("estate","script_model","target_enemy",true,melee::tool::knife) &&
			!trainer::knife_target("trainer","script_origin","target_enemy",true,melee::tool::knife) &&
			!trainer::knife_target("trainer","script_model","target_enemy_extra",true,melee::tool::knife),
			"unrelated maps, linked target bases and similar names are not training targets");
	}
}
