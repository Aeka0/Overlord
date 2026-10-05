#pragma once
#include "component/vr/gameplay/scripted_control.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"
#include "component/vr/gameplay/grip_edges.hpp"
#include "component/vr/gameplay/independent_fire_clock.hpp"

namespace scripted_control_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay;
		using namespace weapons;
		using namespace std::chrono_literals;
		// Captured Team Player: disabled through the link/unlink handoff, then
		// cleared at server time 26266. Ordinary later flags 0x10 remain allowed.
		for (auto flags : {0x80u,0x90u}) check(!scripted_control::permits_weapons(flags), "script prohibition survives other weapon flags");
		check(scripted_control::permits_weapons(0) && scripted_control::permits_weapons(0x10),
			"free play and unrelated weapon flags are not classified as cinematics");
		carry::inventory inventory;
		const std::array<carry::owned_weapon,2> owned{{{64,{}},{65,{}}}};
		check(inventory.reconcile(owned) && inventory.equip_definition(64,hand::left), "seed left-handed pre-scene carry");
		const auto before=*inventory.find_definition(64);
		scripted_control::selection_pause pause;
		pause.suspend(64);pause.suspend(0);
		check(inventory.reconcile(owned) && pause.resume(64) && !pause.suspended() &&
			inventory.find_definition(64)->id==before.id && inventory.find_definition(64)->owner.revision==before.owner.revision &&
			inventory.in_hand(hand::left)->id==before.id,
			"temporary script selection retains instance and left-hand grip without re-equipping");
		pause.suspend(64);
		const std::array<carry::owned_weapon,1> replacement{{{65,{}}}};
		check(inventory.reconcile(replacement) && !pause.resume(65) && !inventory.find(before.id),
			"script weapon removal wins over retained carry at resume");
		pause.suspend(65);check(!pause.resume(0), "explicit final empty selection is not overwritten");
		pause.suspend(65);pause.reset();check(!pause.suspended() && !pause.resume(65), "checkpoint reset cannot restore an old scene snapshot");
		check(inventory.equip_definition(65,hand::left),"hold weapon before lethal scene edge");
		const auto dying=*inventory.find_definition(65);
		check(inventory.support(dying.id,hand::right),"support dying weapon with the second hand");
		pause.suspend(65);inventory.put_away();inventory.put_away();
		check(inventory.reconcile(replacement) && pause.resume(65) && !inventory.in_hand(hand::left) &&
			!inventory.in_hand(hand::right) && inventory.find(dying.id) && !inventory.find(dying.id)->owner.can_fire(),
			"death revokes both grips without dropping native inventory or restoring grips on an unchanged selection");

		vr::controller_input::frame input{};input.focused=true;input.reference_generation=1;
		for (unsigned h=0;h<2;++h) {input.grip[h].valid=input.aim[h].valid=true;input.trigger[h].active=input.squeeze[h].active=true;}
		auto now=vr::controller_input::clock::time_point{}+1s;
		const auto sample=[&](int ms,bool down) {
			now=vr::controller_input::clock::time_point{}+std::chrono::milliseconds(ms);input.sampled_at=now;++input.sequence;
			for (unsigned h=0;h<2;++h) for (auto* b : {&input.trigger[h],&input.squeeze[h]})
			{if (down && !b->down) ++b->presses;if (!down && b->down) ++b->releases;b->down=down;}
		};
		independent_fire::shot_clock shots;
		const independent_fire::timing burst{3,50,0,100};
		const hold owner{64,1,hand::left,hand::none,hold_source::interaction,1};
		carry::grip_edge_gate grips;
		sample(1000,false);shots.due(input,owner,true,true,now,1000,burst);grips.consume(input,true,now);
		sample(1025,true);check(shots.due(input,owner,true,true,now,1025,burst), "start burst before scripted takeover");shots.settled(1025,burst,true);
		shots.cancel();
		for (int ms : {1050,1075,1100})
		{
			sample(ms,ms!=1075);
			const auto edges=grips.consume(input,false,now);
			check(!shots.due(input,owner,false,true,now,ms,burst) && !edges.pressed && !edges.released,
				"scripted interval cannot fire pending burst or apply carry input");
		}
		sample(1125,true);const auto resumed=grips.consume(input,true,now);
		check(!shots.due(input,owner,true,true,now,1125,burst) && !resumed.pressed && !resumed.released,
			"held trigger and grip history cannot replay at scene exit");
		sample(1150,false);shots.due(input,owner,true,true,now,1150,burst);
		sample(1175,true);check(shots.due(input,owner,true,true,now,1175,burst), "release and fresh trigger press restore firing");
	}
}
