#pragma once
#include "component/vr/gameplay/hei_round_policy.hpp"
#include <limits>

namespace hei_round_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::weapons;
		for(std::string_view gun:{"aa12","m1014_reflex","spas12_arctic","striker_woodland","ranger","model1887"})
			check(hei_round::grenade_weapon(1,4,0,gun),"Every primary shotgun family and variant converts one round");
		check(hei_round::grenade_weapon(1,4,3,"ak47_shotgun_attach"),"Underbarrel shotguns use the same grenade policy");
		for(std::string_view gun:{"cheytac","cheytac_silencer_desert","wa2000_arctic","m82_silencer"})
			check(hei_round::grenade_weapon(1,1,0,gun),"M200, WA2000 and Barrett native variants convert");
		for(std::string_view gun:{"m820","wa20000","not_cheytac","m4","m14ebr"})
			check(!hei_round::grenade_weapon(1,1,0,gun),"Unrelated rifles cannot match a partial weapon name");
		check(!hei_round::grenade_weapon(3,8,0,"m82_bipod_stand_thermal"),"Story-owned fixed thermal sniper retains its native route");
		native_bullet_trace::result hit{};
		hit.fraction = .5f; hit.type = 1; hit.entity = 3998;
		hit.position = {5, 6, 7}; hit.normal = {0, 0, 1};
		hei_round::first_contact round;
		check(round.record(hit), "HEI accepts a real world contact");
		for (int pellet{}; pellet < 64; ++pellet)
		{
			hit.position[0] += 1;
			check(!round.record(hit), "Extra pellets and penetration segments cannot multiply the blast");
		}
		check(round.center()[0] == 5 && round.center()[2] == 7.5f, "HEI keeps the original contact outside the surface");
		for (int kind{}; kind < 8; ++kind)
		{
			auto rejected = hit;
			if (kind == 0) rejected.fraction = 1;
			if (kind == 1) rejected.fraction = -1;
			if (kind == 2) rejected.fraction = std::numeric_limits<float>::quiet_NaN();
			if (kind == 3) rejected.position[0] = std::numeric_limits<float>::infinity();
			if (kind == 4) rejected.surface = 4;
			if (kind == 5) rejected.entity = 0;
			if (kind == 6) rejected.type = 0;
			if (kind == 7) rejected.ignored = true;
			hei_round::first_contact next;
			check(!next.record(rejected) && !next.found, "Miss, invalid geometry, sky and local shield cannot create a blast");
			check(next.record(hit), "An invalid pellet does not hide a later real contact");
		}
	}
}
