#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/pp2000/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include <iostream>
#include <vector>

namespace pp2000_profile_tests
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
				r.parent.fill(-1);r.gun=68;r.count=86;r.parent[68]=13;
				models={{glove,0,68},{pp2000::base.receiver,68,18}};
				constexpr std::string_view names[]{"j_gun","j_back_handle","j_back_ring_base","j_front_ring_base","j_reload","j_safety","j_trigger",
					"tag_brass","tag_clip","tag_eotech","tag_flash","tag_red_dot","tag_silencer","tag_thermal_scope","j_back_ring_end","j_bullets","j_front_ring_end","j_reload_end"};
				for(int i=68;i<86;++i){bones[i].name=names[i-68];r.weapon_bones[i]=true;if(i>68)r.parent[i]=68;}
				r.parent[82]=70;r.parent[83]=76;r.parent[84]=71;r.parent[85]=72;r.arms[0].wrist=0;r.arms[1].wrist=1;
				for(size_t i=0;i<pp2000::idle_fingers.size();++i){bones[2+i].name=pp2000::idle_fingers[i].name;r.parent[2+i]=0;}
				for(const auto* item:items)
				{
					int parent=-1;for(int i=68;i<86;++i)if(bones[i].name==item->contract.receiver_parent)parent=i;
					const int start=r.count;models.push_back({item->contract.model,start,item->bones});
					for(int n=0;n<item->bones;++n){r.weapon_bones[start+n]=true;r.parent[start+n]=n?start:parent;}
					bones[start].name=item->contract.root;
					if(!item->contract.muzzle.empty()){bones[start+1].name=item->contract.muzzle;bones[start+1].bind.position={6,0,0};}
					r.count+=item->bones;
				}
				for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
			}
			profile_match resolve() const{return select_profile(models,r,{bones.data(),size_t(r.count)});}
			p::part_rig parts() const{return p::bind_parts(r,{bones.data(),size_t(r.count)},pp2000::physical);}
		};
		unsigned combinations{};
		for(bool silencer:{false,true})for(int optic:{-1,5,6,7,8,9,10,11,12,13})
		for(bool reverse:{false,true})for(auto glove:{"viewhands_us_army","viewhands_arctic"})
		{
			std::vector<const assembly_attachment*> items;
			if(silencer)items.push_back(&rifle_attachments::common[0]);if(optic>=0)items.push_back(&rifle_attachments::common[optic]);
			if(reverse)std::reverse(items.begin(),items.end());fixture f(items,glove);const auto match=f.resolve();const auto parts=f.parts();
			check(match.value==&pp2000::base && (match.muzzle>=0)==silencer,"PP2000 optics/suppressor preserve integral support and correct muzzle");
			check(parts.valid && parts.magazine==76 && parts.bullets==83 && parts.slide==72,"PP2000 magazine and round stack remain separate from reciprocating rod");
			check(bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},pp2000::base).valid,"PP2000 palm/stock/nested handle-tip poses bind");
			++combinations;
		}
		fixture bare({});
		check(bare.parts().fold_end==85,"PP2000 binds only j_reload_end for folding");
		{auto bad=bare;bad.r.parent[85]=68;check(!bad.parts().valid,"PP2000 folding end must remain a direct child of its rod");}
		for(const auto* denied:{&rifle_attachments::common[1],&rifle_attachments::common[14],&rifle_attachments::common[15],&fal::attachments[0]})
		{
			const assembly_attachment* items[]{denied};check(!fixture(items).resolve().value,"PP2000 rejects attachments without an authored mount");
		}
		const assembly_attachment* both[]{&rifle_attachments::common[5],&rifle_attachments::common[7]};check(!fixture(both).resolve().value,"PP2000 rejects two optics");
		const assembly_attachment* one[]{&rifle_attachments::common[0]};fixture wrong(one);wrong.r.parent[86]=0;check(!wrong.resolve().value,"PP2000 suppressor cannot attach to a glove");
		check(!select_profile(bare.models,bare.r).value,"PP2000 model identity alone cannot admit physical writes");
		for(int i:{72,76,83}){auto bad=bare;bad.r.parent[i]=0;check(!bad.parts().valid,"PP2000 malformed magazine/round/action parenting fails admission");}
		for(auto name:{"pp2000","pp2000_reflex","pp2000_silencer"})check(native_reload_profile(name,20,&pp2000::physical)==&pp2000::physical,"PP2000 native stem is independent of p2000 asset name");
		for(auto name:{"p2000","pp2000_","pp20000","PP2000","pp2000/invalid","m203_pp2000"})check(!pp2000::native_family(name),"PP2000 foreign or malformed feed names are rejected");
		check(!native_reload_profile("pp2000",21) && !native_reload_profile("pp2000",40) && !pp2000::native_family(std::string(10000,'x')),"PP2000 candidate requires base capacity and bounded name");
		const auto& d=pp2000::physical;
		check(pp2000::base.aiming==aim_rule::two_hand && d.ammunition.plus_one && !d.ammunition.last_round_lock && !d.ammunition.release_control &&
			d.ammunition.release==mechanics::magazine_release::button,"PP2000 explicit policy combines button magazine release with no bolt release");
		const auto span=sub(pp2000::wrists[0].position,pp2000::wrists[1].position);
		for(auto axis:{vec{0,1,0},vec{0,0,1},vec{-1,0,0}})
			check(length(sub(rotate(aimed_rotation(aim_rule::two_hand,{0,0,0,1},{},scale(axis,length(span)),span),span),scale(axis,length(span))))<.001f,"PP2000 follows both hands when vertical or turned backwards");
		check(p::valid(d.interaction) && p::native_action_recoil(d.interaction) && d.interaction.locked_travel==0,"PP2000 preserves native rod recoil without a locked-back pose");
		check(choose_part_grip(d.slide_grips,d.slide_grips[0].wrist,{},d.slide_grab_low,d.slide_grab_high,39.37007874f).distance_meters<.001f,"PP2000 top-handle contact lands on its actual tip");
		for(auto gun:{anchor{{},{0,0,0,1}},anchor{{20,5,-4},normalize({.2f,-.3f,.1f,.8f})}})
		{
			const auto mag=compose_reload(gun,d.magazine_rest);const auto tip=magazine_tip_in_well(d,gun,p::translate_local(mag,magazine_exit_translation(d,39.37007874f)),39.37007874f);
			check(magazine_alignment(d,gun,mag)>.999f && tip[2]<-.0099f && std::abs(tip[0])<.001f && std::abs(tip[1])<.001f,"PP2000 tilted magazine clears the grip before becoming a free drop");
		}
		check(pp2000::suppress_equip("h2_wpn_pst_pp2000_first_time_pullout") && !pp2000::suppress_equip("viewmodel_pp2000_pullout_l") &&
			!pp2000::suppress_equip("h2_wpn_pst_pp2000_reload_empty"),"PP2000 equip substitution excludes akimbo and reload clips");
		check(d.sound_key(mechanics::effect::magazine_out) && d.sound_key(mechanics::effect::action_close) && !d.sound_key(mechanics::effect::shot),"PP2000 uses own notetrack keys and retains native shot audio");
		std::cout<<"PP2000 assembly combinations checked: "<<combinations<<'\n';return failed;
	}
}
