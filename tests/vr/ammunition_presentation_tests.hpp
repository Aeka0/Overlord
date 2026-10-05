#pragma once
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/aug/profile.hpp"
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/l86/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/m93r/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/p90/profile.hpp"
#include "component/vr/gameplay/weapons/pp2000/profile.hpp"
#include "component/vr/gameplay/weapons/rpd/profile.hpp"
#include "component/vr/gameplay/weapons/tmp/profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/chamber_cartridge.hpp"
#include <iostream>
#include <limits>

namespace ammunition_presentation_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		int failed{};const auto check=[&](bool ok,const char* why){if(!ok){++failed;std::cerr<<"FAIL: "<<why<<'\n';}};
		const auto faces=[](std::span<const scene_models::surface_face_range> rr){unsigned n{};for(auto r:rr)n+=r.last-r.first+1;return n;};
		struct expected {const reload_profile* p;std::array<unsigned,4> triangles;};
		const expected cases[]{
			{&ak47::physical,{3446,3830,4214,4598}},{&tmp::physical,{974,1208,1442,1676}},
			{&acr::physical,{2228,2822,3416,4010}},{&aug::physical,{2412,2748,3084,3420}},
			{&mp5::physical,{2686,3010,3334,3658}},{&fal::physical,{2974,3310,3646,3982}},
			{&pp2000::physical,{600,1272,1944,2616}},{&l86::physical,{9778,10066,10354,10642}},
			{&m9::physical,{564,874,1040,1182}},{&m93r::physical,{564,874,1040,1182}}};
		for(const auto& c:cases)
		{
			const auto* recipe=c.p->magazine_fill();
			check(recipe && valid_magazine_fill(*recipe),"audited magazine has four valid, cumulative face subsets");if(!recipe)continue;
			for(size_t i=0;i<4;++i)check(faces(recipe->faces[i])==c.triangles[i],"0/1/2/3 preserve magazine body and add exactly the reviewed round geometry");
			for(int n:{std::numeric_limits<int>::min(),-1,0,1,2,3,4,1000,std::numeric_limits<int>::max()})
				check(c.p->magazine_subset(n)==size_t(n<0?0:n>3?3:n),"round count saturates before indexing without overflow");
			mechanics::state ammo{};ammo.magazine_inserted=true;ammo.magazine_rounds=1;ammo.held_rounds=3;ammo.chamber_loaded=true;
			check(c.p->magazine_subset(ammo.magazine_rounds)==1 && c.p->magazine_subset(ammo.held_rounds)==3,
				"inserted magazine does not consume spare payload or count the chamber as a magazine round");
			ammo.magazine_rounds=0;
			check(c.p->magazine_subset(ammo.magazine_rounds)==0,"last chambered round leaves an empty magazine visible as empty");
			auto bad=*recipe;bad.faces[2]=bad.faces[1];check(!valid_magazine_fill(bad),"duplicate population levels reject");
			bad=*recipe;bad.low[1][0]=std::numeric_limits<float>::quiet_NaN();check(!valid_magazine_fill(bad),"nonfinite geometry rejects");
			bad=*recipe;bad.bones=257;check(!valid_magazine_fill(bad),"oversized source skeleton rejects");
		}
		unsigned enabled{};
		for(const auto* p:reload_profiles)if(const auto* recipe=p->magazine_fill())
		{
			++enabled;check(valid_magazine_fill(*recipe) && std::string_view(recipe->source)==p->rigid_magazine_source,
				"every enabled camouflage binds its own validated source");
		}
		check(enabled==15,"only fifteen reviewed receiver variants enable counted magazines");
		check(!ak47::desert.magazine_fill() && !ak47::woodland.magazine_fill() && !acr::arctic.magazine_fill() && !aug::plain.magazine_fill(),
			"missing exports never inherit base-skin face indices");
		check(!fn2000::physical.magazine_fill() && !p90::physical.magazine_fill() && !rpd::physical.magazine_fill(),
			"unreviewed multi-round geometry and belt semantics remain outside counted box magazines");
		for(const auto* p:{&m14ebr::physical,&m14ebr::arctic,&cheytac::physical,&cheytac::desert})
		{
			mechanics::state ammo{};ammo.chamber_loaded=true;
			const auto rest=chamber_cartridge_pose(*p,ammo,0,0,39.37007874f);
			const auto pull=chamber_cartridge_pose(*p,ammo,.02f,.2f,39.37007874f);
			check(rest && pull && pull->position[0]<rest->position[0] && pull->rotation==rest->rotation,
				"live chamber cartridge translates with manual bolt, without rotating with bolt lift");
			ammo.magazine_inserted=false;ammo.magazine_rounds=0;
			check(chamber_cartridge_pose(*p,ammo,.02f,.2f,39.37007874f).has_value(),"removed/empty magazine cannot hide its owned chamber round");
			const auto total=mechanics::total_rounds(ammo);
			for(int i=0;i<1000;++i)(void)chamber_cartridge_pose(*p,ammo,.02f,.2f,39.37007874f);
			check(mechanics::total_rounds(ammo)==total,"repeated eye/render sampling never changes ammunition");
			ammo.chamber_loaded=false;
			check(!chamber_cartridge_pose(*p,ammo,.02f,.2f,39.37007874f),"committed live extraction removes chamber geometry exactly once");
			ammo.bolt.spent_case=true;
			check(!chamber_cartridge_pose(*p,ammo,.02f,.2f,39.37007874f),"spent case never borrows a live cartridge mesh");
			ammo.bolt.spent_case=false;ammo.bolt.feeding=true;
			check(!chamber_cartridge_pose(*p,ammo,.02f,.2f,39.37007874f),"in-transit manual feed does not duplicate the chamber mesh");
			ammo={};ammo.chamber_loaded=true;
			check(!chamber_cartridge_pose(*p,ammo,0,0,0) && !chamber_cartridge_pose(*p,ammo,std::numeric_limits<float>::quiet_NaN(),0,40),
				"invalid scale and motion do not publish cartridge transforms");
		}
		check(!chamber_cartridge_pose(tmp::physical,mechanics::state{},0,0,40),"no guessed chamber on an unauthored weapon");
		return failed;
	}
}
