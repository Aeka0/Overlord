#pragma once
#include "component/vr/gameplay/chambering_guide.hpp"
#include "component/vr/gameplay/weapons/m4/reload_profile.hpp"
#include "component/vr/gameplay/weapons/miniuzi/reload_profile.hpp"
#include "component/vr/gameplay/weapons/cheytac/reload_profile.hpp"
#include "component/vr/gameplay/weapons/rpd/reload_profile.hpp"
#include "component/vr/gameplay/weapons/m1014/profile.hpp"
#include "component/vr/gameplay/weapons/aa12/reload_profile.hpp"

template<class Check> void chambering_guide_tests(Check check)
{
	using namespace vr::gameplay::weapons;
	using namespace chambering_guide;
	mechanics::state ammo{1,1,1,true,false,10,30};
	check(needed(m4::physical,ammo) && !catch_needed(m4::physical,ammo),"loaded closed action asks for handle, not an inactive release paddle");
	check(!needed(m4::physical,ammo,true),"holding the action suppresses the yellow guide");
	ammo.action=action_state::locked_open;
	check(needed(m4::physical,ammo) && catch_needed(m4::physical,ammo),"loaded follower lock highlights both valid chambering routes");
	ammo.action=action_state::closed;ammo.chamber_loaded=true;
	check(!needed(m4::physical,ammo),"completed chambering clears the hint");
	ammo.chamber_loaded=false;ammo.magazine_rounds=0;
	check(!needed(m4::physical,ammo),"reserve ammunition alone does not highlight a control");
	ammo.magazine_inserted=false;ammo.held_rounds=10;ammo.magazine_hand=vr::hand::left;
	check(!needed(m4::physical,ammo),"loaded magazine in a hand is not a loaded weapon");
	ammo={1,1,1,true,false,10,30};
	check(needed(miniuzi::physical,ammo),"loaded open-bolt action must be cocked");
	ammo.action=action_state::cocked_open;
	check(!needed(miniuzi::physical,ammo),"ready open bolt never asks for a closed chamber");
	ammo={1,1,1,true,false,4,10};ammo.bolt.spent_case=true;ammo.bolt.cocked=false;
	check(needed(cheytac::physical,ammo),"manual bolt with live magazine rounds requests cycling");
	ammo.magazine_rounds=0;
	check(!needed(cheytac::physical,ammo),"spent casing without live rounds does not request cycling");
	ammo={1,1,1,true,false,10,30};
	check(!needed(rpd::physical,ammo),"unseated belt cannot request charging");
	ammo.belt={1,true,1};
	check(!needed(rpd::physical,ammo),"open cover cannot request charging");
	ammo.belt={0,true,0};
	check(needed(rpd::physical,ammo),"seated belt and closed cover request charging");
	ammo.magazine_rounds=-1;
	check(!needed(rpd::physical,ammo),"invalid mechanical state cannot produce a guide");
	auto shells=tube::import_native(m1014::feed.ammunition,1,1,{0,20});shells.stored=3;
	check(needed(m1014::feed,shells) && needed(m1014::arctic_feed,shells),"loaded M1014 tube with locked empty chamber requests its bolt control");
	check(!needed(m1014::feed,shells,true),"holding M1014 bolt suppresses its guide");
	shells.chamber=true;shells.phase=tube::action::closed;
	check(!needed(m1014::feed,shells),"ready M1014 clears the guide");
	shells.chamber=false;shells.stored=0;
	check(!needed(m1014::feed,shells),"empty M1014 with reserves does not request chambering");
	auto pump=m1014::feed;pump.ammunition.drive=tube::action_drive::pump;
	check(!supported(pump),"M1014 support does not opt pump/lever controls into the bolt-guide scope");
	check(m4::physical.action_detail_bone=="j_reload_trigger" && m4::arctic_physical.action_detail_bone=="j_reload_trigger" &&
		aa12::physical.action_detail_bone=="j_reload_end","audited M4 latch and AA12 upper handle are explicit separately posed guide pieces");
}
