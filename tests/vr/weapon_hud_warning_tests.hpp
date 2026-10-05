#pragma once
#include "component/vr/gameplay/weapon_hud_warning.hpp"

template<class Check> void weapon_hud_warning_tests(Check check)
{
	using namespace vr::gameplay;
	using namespace weapon_hud;
	using namespace weapons;
	const mechanics::rules closed{15,mechanics::magazine_release::button,true,true,true};
	mechanics::state ammo{1,1,1,true,false,15,30};
	check(needs_chamber(closed,ammo),"loaded magazine with empty chamber needs racking");
	check(!needs_chamber(closed,ammo,true),"held action cannot request another chambering operation");
	ammo.chamber_loaded=true;
	check(!needs_chamber(closed,ammo),"chambered weapon does not warn during trigger cooldown or grip manipulation");
	ammo.chamber_loaded=false;ammo.magazine_rounds=0;
	check(!needs_chamber(closed,ammo),"empty magazine with reserve ammo does not request chambering");
	ammo.magazine_inserted=false;ammo.held_rounds=15;ammo.magazine_hand=hand::left;
	check(!needs_chamber(closed,ammo),"detached loaded spare does not count as loaded gun ammo");
	const mechanics::rules open{30,mechanics::magazine_release::physical_pull,false,false,false,mechanics::feed_type::open_bolt};
	ammo={2,1,1,true,false,30,60};
	check(needs_chamber(open,ammo),"uncocked open-bolt gun with ammunition needs charging");
	ammo.action=action_state::cocked_open;
	check(!needs_chamber(open,ammo),"ready open-bolt gun is not misclassified by its always empty chamber");
	const mechanics::rules bolt{5,mechanics::magazine_release::physical_pull,false,false,false,mechanics::feed_type::manual_bolt};
	ammo={3,1,1,true,false,4,10};ammo.bolt.spent_case=true;ammo.bolt.cocked=false;
	check(needs_chamber(bolt,ammo),"manual rifle requests cycling after a shot with magazine ammo left");
	ammo.magazine_rounds=0;
	check(!needs_chamber(bolt,ammo),"spent case alone is not live ammunition");
	ammo.magazine_rounds=-1;
	check(!needs_chamber(bolt,ammo),"invalid ammo state cannot produce a chambering instruction");
	const tube::rules pump{8,tube::action_drive::pump};
	auto shells=tube::import_native(pump,4,1,{4,20});
	check(!needs_chamber(pump,shells),"ready pump shotgun hides prompt");
	shells.chamber=false;shells.spent_case=true;
	check(needs_chamber(pump,shells),"loaded tube requests pumping after firing");
	const tube::rules drum{12,tube::action_drive::automatic,tube::feed_layout::fixed_drum};
	check(!needs_chamber(drum,tube::import_native(drum,5,1,{12,24})),"loaded fixed drum does not require a persistent chamber");
	auto secondary=underbarrel::import_native({{6,1},7,underbarrel::kind::shotgun},4,20);
	check(!needs_chamber(secondary),"ready underbarrel feed hides prompt independently");
	secondary.chamber=false;secondary.spent=true;--secondary.loaded;
	check(needs_chamber(secondary),"underbarrel shotgun requests its own cycle");
	secondary.loaded=0;
	check(!needs_chamber(secondary),"empty underbarrel feed does not warn");
	check(select_messages(false,false,1,20)==warning_messages{status_line::none,status_line::none},
		"a final chambered round still counts as loaded ammunition");
	check(select_messages(false,false,0,20)==warning_messages{status_line::magazine_empty,status_line::none},
		"empty magazine and chamber request a steady empty-magazine caption");
	check(select_messages(false,false,10,0)==warning_messages{status_line::none,status_line::none} &&
		select_messages(true,false,10,0)==warning_messages{status_line::chamber,status_line::none} &&
		select_messages(false,false,1,0)==warning_messages{status_line::none,status_line::none} &&
		select_messages(false,false,0,0)==warning_messages{status_line::no_ammo,status_line::none},
		"no ammo requires empty chamber magazine and reserve and replaces magazine empty");
	check(select_messages(false,true,0,0)==warning_messages{status_line::quick_reload,status_line::no_ammo} &&
		!has_message(select_messages(true,false,-1,0)),"loading replaces the empty caption and invalid ammunition has no warning");
	check(status_color(status_line::chamber)==std::array<std::uint8_t,3>{255,215,0} &&
		status_color(status_line::magazine_empty)==std::array<std::uint8_t,3>{255,128,128} &&
		status_color(status_line::no_ammo)==status_color(status_line::magazine_empty),"chamber caption is yellow and ammo captions use soft red");
	chamber_warning steady;
	for(const auto now:{0u,499u,500u,999u,1000u})
		check(status_visible(steady,status_line::magazine_empty,now) && status_visible(steady,status_line::no_ammo,now),"empty and no-ammo captions never blink");
	chamber_warning left,right;
	check(left.visible(true,100) && left.visible(true,599) && !left.visible(true,600) && left.visible(true,1100),
		"warning starts visible and alternates every 500 ms");
	check(right.visible(true,600) && !left.visible(true,1600),"hands have independent warning phases");
	check(!left.visible(false,1700) && left.visible(true,1750),"ready state clears warning immediately and next warning starts visible");
	check(left.visible(true,1),"clock rollback restarts the visible phase");
	check(select_status(true,true)==status_line::quick_reload && select_status(false,true)==status_line::quick_reload &&
		select_status(true,false)==status_line::chamber && select_status(false,false)==status_line::none,
		"quick reload takes priority in the existing warning row, including an empty gun");
	for(const auto now:{1000u,1500u,1999u,2000u,2500u})
		check(status_visible(left,status_line::quick_reload,now),"quick reload text never blinks at a chamber-warning boundary");
	check(status_visible(left,status_line::chamber,2501) && !status_visible(left,status_line::chamber,3001),
		"loading completion restores chamber warning with a fresh visible phase");
	check(!status_visible(right,status_line::none,2501),"cancellation immediately clears an otherwise unneeded status row");
	const float offset=warning_clearance(.074f,.074f,.05f);
	check(.05f+offset>=.074f+warning_row_meters-.00001f && warning_clearance(.03f,.03f,.08f)==0,
		"secondary row clears the entire primary warning without moving during blink-off phases");
}
