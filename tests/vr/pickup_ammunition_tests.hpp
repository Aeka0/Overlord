#pragma once
#include "component/vr/gameplay/ammunition_transfer.hpp"
#include "component/vr/gameplay/world_pickup_policy.hpp"
#include <algorithm>

namespace pickup_ammunition_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons::ammunition;
		using vr::gameplay::weapons::carry::suppress_weapon_touch;
		using vr::gameplay::weapons::carry::pickup_context;
		using vr::gameplay::weapons::carry::admit_duplicate_pickup;
		for (const auto context:{pickup_context::hand_query,pickup_context::grip_transfer})
		{
			check(admit_duplicate_pickup(context,true,true,0,0,0),"owned firearm survives both hand candidate filtering and final grip admission");
			check(!admit_duplicate_pickup(context,false,true,0,0,0) && !admit_duplicate_pickup(context,true,false,0,0,0),
				"stale/foreign items and unsupported feeds retain native admission");
			for (const int value:{-1,1,2})
				check(!admit_duplicate_pickup(context,true,true,value,0,0) && !admit_duplicate_pickup(context,true,true,0,value,0),
					"automatic pickup and native akimbo never enter a duplicate grip transaction");
			check(!admit_duplicate_pickup(context,true,true,0,0,0x80) && !admit_duplicate_pickup(context,true,true,0,0,0x8000),
				"hand candidate and grip paths both preserve native script pickup restrictions");
		}
		check(!admit_duplicate_pickup(pickup_context::native,true,true,0,0,0),"ordinary native use lists do not gain a global duplicate bypass");
		check(suppress_weapon_touch(true,true,true,false),"walking over owned or unowned firearms cannot consume their ammo or entities");
		check(!suppress_weapon_touch(true,true,true,true),"the exact admitted grip pickup retains native ammo transfer and notifications");
		check(!suppress_weapon_touch(false,true,true,false) && !suppress_weapon_touch(true,false,true,false),"flat gameplay and other actors retain native pickup behavior");
		check(!suppress_weapon_touch(true,true,false,false),"ordinary ammo and mission items keep native touch handling");
		const auto colt=restore_pickup({7,89},8,8,7,8);
		check(colt && *colt==projection{8,89},"1911 recorded 7+1 drop restores the native clipped chamber round without changing reserve");
		check(restore_pickup({8,89},8,8,7,8)==colt,"already preserved +1 pickup does not duplicate ammunition");
		for (int loaded=0;loaded<=8;++loaded)
		{
			const auto picked=restore_pickup({std::min(loaded,7),23},loaded,loaded,7,8);
			check(picked && *picked==projection{loaded,23},"empty/chamber-only/partial/full recorded pickups preserve exact feed and reserve");
		}
		check(!restore_pickup({7,89},8,7,7,8),"changed world payload cannot recover a different saved chamber");
		check(!restore_pickup({6,89},8,8,7,8),"unexplained native consumption is not overwritten as a pickup clamp");
		check(!restore_pickup({7,89},8,8,7,7),"profiles without +1 cannot gain a chamber round");
		check(!restore_pickup({7,-1},8,8,7,8),"invalid native reserve blocks pickup restoration");
		const auto cleanup=release_fault({7,89},3);
		check(cleanup && *cleanup==projection{7,92},"faulted weapon release returns only escrow and retains actual native loaded count");
		check(cleanup && release_fault(*cleanup,0)==cleanup,"repeated fault preparation cannot refund escrow twice");
		check(!release_fault({7,1000000},1) && !release_fault({7,89},-1),"fault release rejects overflow and invalid escrow without losing its rounds");
	}
}
