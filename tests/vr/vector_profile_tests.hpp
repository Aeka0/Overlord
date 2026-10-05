#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/vector/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/part_return_transition.hpp"
#include "component/vr/gameplay/rigid_part_visibility.hpp"
#include <iostream>
#include <limits>
#include <vector>

namespace vector_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		namespace p=physical_reload;
		int failed{}; const auto check=[&](bool ok,const char* why) { if (!ok) { ++failed; std::cerr<<"FAIL: "<<why<<'\n'; } };
		struct fixture
		{
			rig r{}; std::array<bone_definition,256> bones{}; std::vector<model_definition> models;
			fixture(size_t skin,std::span<const assembly_attachment* const> items,std::string_view glove="viewhands_us_army")
			{
				r.parent.fill(-1); r.gun=68; r.count=86; r.parent[68]=13;
				models={{glove,0,68},{vector::skins[skin]->rigid_magazine_source,68,18}};
				constexpr std::string_view names[]{"j_gun","j_bolt","j_handle","j_reload","j_switch","j_trigger","tag_acog_2",
					"tag_brass","tag_clip","tag_eotech","tag_flash","tag_foregrip","tag_red_dot","tag_sight_off","tag_sight_on",
					"tag_silencer","tag_thermal_scope","j_bullet"};
				for (int i=68;i<86;++i) { bones[i].name=names[i-68]; r.weapon_bones[i]=true; if (i>68) r.parent[i]=68; }
				r.parent[85]=76;
				for (const auto* item:items)
				{
					int parent=-1; for (int i=68;i<86;++i) if (bones[i].name==item->contract.receiver_parent) parent=i;
					const auto start=r.count; models.push_back({item->contract.model,start,item->bones});
					for (int n=0;n<item->bones;++n) { r.weapon_bones[start+n]=true; r.parent[start+n]=n ? start : parent; }
					bones[start].name=item->contract.root;
					if (!item->contract.muzzle.empty()) { bones[start+1].name=item->contract.muzzle; bones[start+1].bind.position={6.68582f,0,0}; }
					r.count+=item->bones;
				}
				r.arms[0].wrist=0; r.arms[1].wrist=1;
				for (size_t i=0;i<vector::idle_fingers.size();++i)
				{
					bones[2+i].name=vector::idle_fingers[i].name;
					r.parent[2+i]=bones[2+i].name.find("_le_")!=std::string_view::npos ? 0 : 1;
				}
				for (int i=0;i<r.count;++i) { bones[i].bind.rotation={0,0,0,1}; bones[i].parent=r.parent[i]; }
			}
			profile_match resolve() const { return select_profile(models,r,{bones.data(),size_t(r.count)}); }
			p::part_rig parts(const reload_profile& d=vector::physical) const { return p::bind_parts(r,{bones.data(),size_t(r.count)},d); }
		};
		unsigned combinations{};
		for (size_t skin=0;skin<2;++skin) for (bool silencer:{false,true}) for (int optic=-1;optic<13;++optic)
		for (bool reverse:{false,true}) for (auto glove:{"viewhands_us_army","viewhands_arctic","viewhands_other"})
		{
			std::vector<const assembly_attachment*> items;
			if (silencer) items.push_back(&rifle_attachments::common[0]); if (optic>=0) items.push_back(&rifle_attachments::common[1+optic]);
			if (reverse) std::reverse(items.begin(),items.end());
			fixture f(skin,items,glove); const auto match=f.resolve(); const auto* expected=&vector::assemblies[skin];
			check(match.value==expected && expected->reload==vector::skins[skin],"Vector accessories preserve the exact receiver magazine skin and common foregrip");
			check((match.muzzle>=0)==silencer,"Vector suppressor has its own validated muzzle");
			const auto parts=f.parts(*expected->reload);
			check(parts.valid && parts.slide==71 && parts.bolt==69 && parts.magazine==76 && parts.bullets==85,
				"Vector binds the folding handle separately from the bolt, stock and tilted magazine");
			check(bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},*expected).valid,"all Vector variants bind native fingers and eight parent-local parts");
			check(native_reload_profile("kriss_silencer",30,expected->reload)==expected->reload,"Vector family discovery preserves selected scene skin");
			++combinations;
		}
		fixture bare(0,{});
		check(bare.resolve().value==&vector::assemblies[0] && !select_profile(bare.models,bare.r).value,"Vector integral foregrip requires topology, without a separate attachment");
		for (const auto* denied:{&rifle_attachments::common[14],&rifle_attachments::common[15],&acr::attachments[0],&ak47::attachments[1]})
		{
			const assembly_attachment* items[]{denied};
			check(!fixture(0,items).resolve().value,"Vector cannot fabricate a missing sensor, laser or underbarrel mount");
		}
		const assembly_attachment* duplicate[]{&rifle_attachments::common[1],&rifle_attachments::common[5]};
		check(!fixture(0,duplicate).resolve().value,"two Vector optics reject regardless of distinct aliases");
		const assembly_attachment* silencer[]{&rifle_attachments::common[0]}; fixture suppressed(1,silencer);
		suppressed.r.parent[86]=0; check(!suppressed.resolve().value,"Vector suppressor cannot attach to a glove");
		suppressed.r.parent[86]=83; suppressed.bones[87].bind.position={-1,0,0};
		check(!suppressed.resolve().value,"backward Vector suppressor muzzle fails admission");
		suppressed.models.back().name="unreviewed_vector_attachment";
		check(!suppressed.resolve().value,"unknown Vector attachment does not inherit the grip recipe");
		for (int i:{69,71,85})
		{ auto malformed=bare; malformed.r.parent[i]=0; check(!malformed.parts().valid,"Vector missing or misparented moving roots fail physical admission"); }
		bare.bones[79].name="missing_foregrip";
		check(!bind_weapon_poses(bare.r,{bare.bones.data(),size_t(bare.r.count)},vector::assemblies[0]).valid,"native integral foregrip is required by the complete pose contract");
		for (auto name:{"kriss","kriss_reflex","kriss_silencer","kriss_acog_black"})
			check(native_reload_profile(name,30)==&vector::physical,"Vector candidate native family uses bounded common primary-ammo admission");
		for (auto name:{"vector","kriss_","kriss2","KRISS","kriss/invalid","m203_kriss","tmp"})
			check(!vector::native_family(name),"foreign, alternate or malformed names cannot select Vector authority");
		check(!vector::native_family(std::string(10000,'a')) && !native_reload_profile("kriss",31) && !native_reload_profile("kriss",1),"Vector rejects oversized names and wrong base capacities");
		const auto& a=vector::assemblies[0]; const auto& b=vector::assemblies[1]; const auto& d=vector::physical;
		check(a.wrists[0].position==b.wrists[0].position && a.wrists[0].rotation==b.wrists[0].rotation && a.fingers.data()==b.fingers.data(),
			"Vector skins never introduce a different support position or finger pose");
		check(a.aiming==aim_rule::two_hand && d.ammunition.plus_one && d.ammunition.last_round_lock && d.ammunition.release_control,
			"Vector retains two-hand aim, chamber plus-one and both M4-style release paths");
		const auto span=sub(a.wrists[0].position,a.wrists[1].position);
		for (auto direction:{vec{0,1,0},vec{0,0,1},vec{-1,0,0}})
			check(length(sub(rotate(aimed_rotation(a.aiming,{0,0,0,1},{},scale(direction,length(span)),span),span),scale(direction,length(span))))<.001f,
				"Vector native baseline remains stable when vertical or reversed");
		check(choose_part_grip(d.slide_grips,d.slide_grips[0].wrist,{},d.slide_grab_low,d.slide_grab_high,39.37007874f).distance_meters<.001f,
			"Vector unfolded grasp acquires on the corresponding real folded tab contact");
		for (auto gun:{anchor{{},{0,0,0,1}},anchor{{200,-50,12},normalize({.2f,-.4f,.1f,.8f})}})
		{
			const auto seated=compose_reload(gun,d.magazine_rest);
			check(magazine_alignment(d,gun,seated)>.999f,"Vector magazine bind tilt is applied once and follows the receiver");
			const auto exited=p::translate_local(seated,magazine_exit_translation(d,39.37007874f));
			const auto tip=magazine_tip_in_well(d,gun,exited,39.37007874f);
			check(tip[2]<=-.0099f && std::abs(tip[0])<.001f && std::abs(tip[1])<.001f,"Vector magazine clears the tilted well before free drop");
		}
		fixture fresh(0,{}); const auto parts=fresh.parts(); part_mask hidden=parts.bullet_mask; hidden[76/32]|=0x80000000u>>(76%32);
		const std::array<rigid_group_range,2> mag{{{8*64,458,0,504},{17*64,180,504,224}}};
		check(plan_rigid_visibility(mag,68,638,728,parts.bullet_mask).hidden_groups==2 && plan_rigid_visibility(mag,68,638,728,hidden).hidden_groups==3,
			"Vector shared magazine surface hides body and round independently");
		const std::array<rigid_group_range,4> action{{{64,148,0,128},{3*64,406,128,393},{4*64,154,521,144},{5*64,300,665,305}}};
		check(plan_rigid_visibility(action,68,1008,970,hidden).hidden_groups==0,"Vector magazine removal cannot hide bolt, handle, release or trigger");
		check(p::valid(d.interaction) && valid_bolt(vector::internal_bolt,d.interaction.slide_stroke) && !p::native_action_recoil(d.interaction),"Vector native firing keeps the folding handle forward");
		check(std::abs(bolt_travel(vector::internal_bolt,0,true)-.03940248f)<1e-6f && bolt_travel(vector::internal_bolt,0,false)==0,
			"Vector internal bolt can remain locked while the released handle returns forward");
		const quat rest{0,0,0,1}; const auto deployed=folded_handle_rotation(rest,d.handle_fold,1);
		check(length(sub(rotate(deployed,{1,0,0}),{0,1,0}))<.001f && folded_handle_rotation(rest,nullptr,1)==rest,
			"Vector handle opens 90 degrees; nonfolding weapons retain their native orientation");
		p::part_return_transition fold; const auto start=vr::controller_input::clock::now();
		check(fold.update(1,1,true,1,start,.075f)==1,"grasp unfolds even without an axial pull");
		check(fold.update(1,1,false,0,start+std::chrono::milliseconds(1),.075f)==1,"release begins at the held orientation");
		const auto middle=fold.update(1,1,false,0,start+std::chrono::milliseconds(31),.075f);
		check(middle>0 && middle<1 && fold.update(1,1,false,0,start+std::chrono::milliseconds(80),.075f)==0 && !fold.active(),
			"folding return completes within its finite visual duration without mechanical gating");
		fold.update(1,1,true,1,start+std::chrono::milliseconds(90),.075f);
		check(fold.update(2,1,false,0,start+std::chrono::milliseconds(100),.075f)==0,"switching Vector instances discards an old unfolding pose");
		fold.update(2,1,true,1,start+std::chrono::milliseconds(110),.075f);
		check(fold.update(2,2,false,0,start+std::chrono::milliseconds(120),.075f)==0,"tracking reference change cannot retain a folded-handle gesture");
		for (float value:{0.f,2.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
		{
			charging_handle_fold invalid{{0,0,0,value}}; auto bad=d; bad.handle_fold=&invalid;
			check(!fresh.parts(bad).valid && folded_handle_rotation(rest,&invalid,1)==rest,"invalid folding orientation fails admission and finite presentation");
		}
		auto bad=d; bad.interaction.motion=p::action_motion::reciprocating_slide;
		check(!fresh.parts(bad).valid,"fold descriptor cannot silently turn a reciprocating slide into a handle");
		check(vector::suppress_equip("h2_wpn_smg_kriss_first_pullout") &&
			equip_presentation_index(40,1,"h2_wpn_smg_kriss_first_pullout","h2_wpn_smg_kriss_idle",true,0,false)==1,
			"Vector first_pullout stock unfolding is suppressed by both presentation boundaries");
		for (auto suffix:{"fire","reload_empty","inspect","akimbo_r_pullout"})
			check(!vector::suppress_equip(std::string("h2_wpn_smg_kriss_")+suffix),"Vector keeps native firing and unrelated actions outside equip suppression");
		check(!d.sound_key(mechanics::effect::shot) && !d.sound_key(mechanics::effect::action_rear),"Vector compound charging and native firing audio are not doubled");
		std::cout<<"Vector assembly combinations checked: "<<combinations<<'\n'; return failed;
	}
}
