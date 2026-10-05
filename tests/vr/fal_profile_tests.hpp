#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/rigid_part_visibility.hpp"
#include <iostream>
#include <vector>

namespace fal_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;namespace p=physical_reload;
		int failed{};const auto check=[&](bool ok,const char* why){if (!ok){++failed;std::cerr<<"FAIL: "<<why<<'\n';}};
		struct fixture
		{
			rig r{};std::array<bone_definition,256> bones{};std::vector<model_definition> models;
			fixture(std::span<const assembly_attachment* const> items,std::string_view glove="viewhands_us_army")
			{
				r.parent.fill(-1);r.gun=68;r.count=89;r.parent[68]=13;
				models={{glove,0,68},{fal::bare.receiver,68,21}};
				constexpr std::string_view names[]{"j_gun","j_bolt","j_clip_release","j_ring_base","j_trigger","tag_acog_2","tag_brass",
					"tag_clip","tag_clip_02","tag_eotech","tag_flash","tag_heartbeat","tag_m203","tag_red_dot","tag_shotgun",
					"tag_sight_on","tag_silencer","tag_thermal_scope","j_bullet","j_bullet_02","j_ring_end"};
				for (int i=68;i<89;++i){bones[i].name=names[i-68];r.weapon_bones[i]=true;if(i>68)r.parent[i]=68;}
				r.parent[86]=75;r.parent[87]=76;r.parent[88]=71;r.arms[0].wrist=0;r.arms[1].wrist=1;
				for (size_t i=0;i<fal::idle_fingers.size();++i){bones[2+i].name=fal::idle_fingers[i].name;r.parent[2+i]=0;}
				for (const auto* item:items)
				{
					int parent=-1;for(int i=68;i<89;++i)if(bones[i].name==item->contract.receiver_parent)parent=i;
					const int start=r.count;models.push_back({item->contract.model,start,item->bones});
					for(int n=0;n<item->bones;++n){r.weapon_bones[start+n]=true;r.parent[start+n]=n?start:parent;}
					bones[start].name=item->contract.root;
					if(!item->contract.muzzle.empty()){bones[start+1].name=item->contract.muzzle;bones[start+1].bind.position={6,0,0};}
					r.count+=item->bones;
				}
				for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
			}
			profile_match resolve() const{return select_profile(models,r,{bones.data(),size_t(r.count)});}
			p::part_rig parts(const reload_profile& d=fal::physical) const{return p::bind_parts(r,{bones.data(),size_t(r.count)},d);}
		};
		unsigned combinations{};
		for(bool shotgun:{false,true})for(bool silencer:{false,true})for(int optic=-1;optic<13;++optic)
		for(bool reverse:{false,true})for(auto glove:{"viewhands_us_army","viewhands_arctic"})
		{
			std::vector<const assembly_attachment*> items;
			if(shotgun)items.push_back(&fal::attachments[0]);if(silencer)items.push_back(&rifle_attachments::common[0]);
			if(optic>=0)items.push_back(&rifle_attachments::common[1+optic]);if(reverse)std::reverse(items.begin(),items.end());
			fixture f(items,glove);auto match=f.resolve();const auto* expected=shotgun?&fal::shotgun:&fal::bare;
			check(match.value==expected && expected->reload==&fal::physical,"FAL attachment combinations select shared mechanics and correct support grip");
			check((match.muzzle>=0)==silencer && bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},*expected).valid,"FAL attachment muzzle and complete native hand/part poses bind");
			const auto parts=f.parts();check(parts.valid && parts.magazine==76 && parts.bullets==87 && parts.slide==69 && parts.partition_root==69,"FAL partitions the shared bolt/handle bone and preserves the seated magazine and round");
			check(native_reload_profile(shotgun?"fal_shotgun":"fal",20,expected->reload)==&fal::physical,"FAL native family keeps primary twenty-round authority");
			++combinations;
		}
		fixture f({});const auto parts=f.parts();
		check(fal::bare.aiming==aim_rule::two_hand && fal::bare.wrists[1].position==fal::shotgun.wrists[1].position &&
			fal::bare.wrists[0].position!=fal::shotgun.wrists[0].position && free_hand_rotation(fal::bare,0)==free_hand_rotation(fal::shotgun,0),
			"FAL shotgun changes support contact without changing rear or free-hand basis");
		const auto marked=[](const part_mask& mask,int i){return (mask[i/32]&(0x80000000u>>(i%32)))!=0;};
		for(int i=0;i<f.r.count;++i)check(marked(parts.animation_only_magazines,i)==(i==75 || i==86),"only native spare copy and its child round are suppressed");
		const std::array<rigid_group_range,4> groups{{{7*64,2034,0,2974},{8*64,2034,2974,2974},{18*64,669,5948,1008},{19*64,669,6956,1008}}};
		const auto visible=plan_rigid_visibility(groups,68,5406,7964,parts.animation_only_magazines);
		check(visible.valid && visible.hidden_groups==5,"shared FAL material retains seated magazine groups when cosmetic copy hides");
		for(auto name:{"tag_clip_02","j_bolt","j_gun","missing","j_bullet_02"})
		{
			auto d=fal::physical;const std::string_view roots[]{name};d.animation_only_magazines=roots;
			check(!f.parts(d).valid,"animation-only mask cannot consume live parts or invalid roots");
		}
		{
			auto d=fal::physical;const std::string_view roots[]{"tag_clip","tag_clip"};d.animation_only_magazines=roots;
			check(!f.parts(d).valid,"duplicate cosmetic roots fail the whole mask");
			d.animation_only_magazines={};check(f.parts(d).animation_only_magazines==part_mask{},"existing profiles without cosmetic copies keep an empty mask");
		}
		for(int index:{75,76,87}){auto bad=f;bad.r.parent[index]=0;check(!bad.parts().valid,"FAL magazine and duplicate roots must remain receiver-owned");}
		const assembly_attachment* duplicate[]{&fal::attachments[0],&fal::attachments[0]};check(!fixture(duplicate).resolve().value,"FAL rejects duplicate shotguns");
		const assembly_attachment* launcher[]{&acr::attachments[0]};check(!fixture(launcher).resolve().value,"unreviewed FAL M203 cannot inherit shotgun grip");
		const assembly_attachment* shot[]{&fal::attachments[0]};fixture wrong(shot);wrong.r.parent[89]=0;check(!wrong.resolve().value,"FAL shotgun cannot attach to a hand");
		check(!select_profile(f.models,f.r).value,"FAL scene needs topology, not just model names");
		for(auto name:{"fal_","fal2","fn_fal","shotgun_fal","m203_fal","fal/invalid"})check(!fal::native_family(name),"FAL malformed/alternate names are rejected");
		check(!native_reload_profile("fal",21) && !native_reload_profile("fal_shotgun",4) && !fal::native_family(std::string(10000,'x')),"FAL capacity and bounded identity guards remain mandatory");
		const auto& d=fal::physical;
		check(p::valid(d.interaction) && !p::native_action_recoil(d.interaction) && d.ammunition.last_round_lock && d.ammunition.release_control &&
			d.ammunition.release==mechanics::magazine_release::physical_pull,"FAL charging handle combines physical magazine latch with button follower release");
		check(choose_part_grip(d.slide_grips,d.slide_grips[0].wrist,{},d.slide_grab_low,d.slide_grab_high,39.37007874f).distance_meters<.001f,"native left grip contact meets FAL charging tab");
		for(auto gun:{anchor{{},{0,0,0,1}},anchor{{20,5,-4},normalize({.2f,-.3f,.1f,.8f})}})
		{
			const auto mag=compose_reload(gun,d.magazine_rest);const auto tip=magazine_tip_in_well(d,gun,p::translate_local(mag,magazine_exit_translation(d,39.37007874f)),39.37007874f);
			check(tip[2]<-.0099f && std::abs(tip[0])<.001f && std::abs(tip[1])<.001f,"FAL detached body clears authored mouth in a rotated gun frame");
		}
		check(fal::suppress_equip("h2_wpn_asl_fn_fal_first_pullout") && fal::suppress_equip("h2_wpn_asl_fn_fal_shotgun_pullout") &&
			!fal::suppress_equip("h2_wpn_asl_fn_fal_shotgun_shotty_pullout"),"FAL equip suppression stays in rifle animation modes");
		check(!fal::sound_key(mechanics::effect::action_rear) && fal::sound_key(mechanics::effect::action_close) && !fal::sound_key(mechanics::effect::shot),"FAL compound chamber and native shot audio are not doubled");
		std::cout<<"FAL assembly combinations checked: "<<combinations<<'\n';return failed;
	}
}
