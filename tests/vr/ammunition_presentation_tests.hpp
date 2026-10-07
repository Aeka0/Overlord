#pragma once
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/aug/profile.hpp"
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/de50/profile.hpp"
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
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/g18/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
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
			{&m9::physical,{564,874,1040,1182}},{&m93r::physical,{564,874,1040,1182}},
			{&de50::physical,{812,1134,1456,1778}},{&de50::gold_physical,{812,1134,1456,1778}},
			{&scar::physical,{2448,2784,2965,3125}}};
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
		check(enabled==42,"all forty-two reviewed counted-magazine profiles are registered");
		for (const auto* definition : reload_profiles)
		{
			const auto* fill = definition->magazine_fill();
			if (!fill || !fill->stack) continue;
			check(definition->magazine_subset_count() == 4 && definition->magazine_subset(INT_MAX) == 3,
				"authored stacks use bounded four-state population independently of extreme ammo counts");
			for (const auto& round : fill->stack->rounds)
				check(!round.faces.empty(), "every authored cartridge keeps an explicit source face selection");
			auto invalid_stack = *fill->stack;
			invalid_stack.rounds[2].translation[0] = std::numeric_limits<float>::quiet_NaN();
			auto invalid_recipe = *fill;
			invalid_recipe.stack = &invalid_stack;
			check(!valid_magazine_fill(invalid_recipe), "nonfinite copy placement cannot enter asset preparation");
			if (!fill->stack->follower_faces.empty())
			{
				const auto& travel = fill->stack->follower_translations;
				check(travel[0] == vec{} && travel[3][2] < travel[1][2] && travel[1][2] <= 0,
					"magazine support remains present and descends beneath the visible rounds");
				invalid_stack = *fill->stack;
				invalid_stack.follower_faces = fill->faces[0];
				check(!valid_magazine_fill(invalid_recipe), "moving support cannot duplicate permanent body faces");
				invalid_stack = *fill->stack;
				invalid_stack.follower_translations[2][2] = std::numeric_limits<float>::quiet_NaN();
				check(!valid_magazine_fill(invalid_recipe), "invalid support placement rejects the complete state set");
			}
		}
		check(g18::physical.receiver_parented_bullets && g18::physical.magazine_fill() &&
			tavor::physical.magazine_fill() && !tavor::physical.magazine_body_bones.empty() &&
			cheytac::physical.magazine_fill() && cheytac::physical.feeding_path,
			"counted population coexists with receiver parenting, permanent structure and manual feeding");
		mechanics::state presentation_state{};
		check(hide_counted_magazine_geometry(g18::physical, presentation_state, false, true) &&
			!hide_counted_magazine_geometry(g18::physical, presentation_state, false, false),
			"receiver-parented magazine rounds are replaced without hiding unrelated receiver bones");
		check(!hide_counted_magazine_geometry(m14ebr::physical, presentation_state, true, true) &&
			hide_counted_magazine_geometry(m14ebr::physical, presentation_state, true, false),
			"counted M14 magazine replaces its body but never captures the original chamber round");
		check(hide_counted_magazine_geometry(cheytac::physical, presentation_state, true, true),
			"resting M200 top round is supplied by its counted magazine");
		presentation_state.bolt.feeding = true;
		check(!hide_counted_magazine_geometry(cheytac::physical, presentation_state, true, true),
			"M200 in-transit round retains independent native visibility during feeding");
		const auto* standard_deagle = de50::physical.magazine_fill();
		const auto* gold_deagle = de50::gold_physical.magazine_fill();
		check(standard_deagle && gold_deagle && standard_deagle != gold_deagle &&
			standard_deagle->surfaces.size() == 7 && gold_deagle->surfaces.size() == 5,
			"gold Desert Eagle keeps its captured surface layout rather than inheriting base face indices");
		const auto deagle_mesh = de50::physical.magazine_mesh();
		check(deagle_mesh.count == 3 && deagle_mesh.names[2] == "j_bullet01" &&
			de50::physical.bullet_parents[0].parent == "tag_bullets",
			"all three Deagle rounds include the nested top cartridge without changing its parent");
		check(!ak47::desert.magazine_fill() && !ak47::woodland.magazine_fill() && !acr::arctic.magazine_fill() && !aug::plain.magazine_fill(),
			"missing exports never inherit base-skin face indices");
		check(fn2000::physical.magazine_fill() && !fn2000::physical.magazine_fill()->stack &&
			!p90::physical.magazine_fill() && !rpd::physical.magazine_fill(),
			"F2000 uses original face levels while transparent native bands and belts retain their policies");
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
