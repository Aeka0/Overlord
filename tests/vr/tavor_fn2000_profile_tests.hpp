#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "tavor_fn2000_data.hpp"
#include <iostream>
#include <vector>

namespace tavor_fn2000_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		int failed{};const auto check=[&](bool ok,const char* why){if(!ok){++failed;std::cerr<<"FAIL: "<<why<<'\n';}};
		struct fixture
		{
			rig r{};std::array<bone_definition,256> bones{};std::vector<model_definition> models;const profile* definition;
			fixture(const profile& d,std::span<const assembly_attachment* const> items,std::string_view glove="viewhands_us_army"):definition(&d)
			{
				r.parent.fill(-1);r.gun=68;r.parent[68]=13;r.arms[0].wrist=0;r.arms[1].wrist=1;
				const std::span<const tavor_fn2000_data::bone> source=d.id=="fn2000" ? std::span<const tavor_fn2000_data::bone>(tavor_fn2000_data::fn2000) :
					d.reload==&tavor::digital ? std::span<const tavor_fn2000_data::bone>(tavor_fn2000_data::digital) : std::span<const tavor_fn2000_data::bone>(tavor_fn2000_data::tavor);
				r.count=68+static_cast<int>(source.size());models={{glove,0,68},{d.receiver,68,r.count-68}};
				for(size_t i=0;i<d.fingers.size();++i){bones[2+i].name=d.fingers[i].name;r.parent[2+i]=0;}
				for(size_t i=0;i<source.size();++i){bones[68+i].name=source[i].name;r.weapon_bones[68+i]=true;if(i)r.parent[68+i]=68+source[i].parent;}
				const auto end=r.count;
				for(const auto* item:items)
				{
					int parent=-1;for(int i=68;i<end;++i)if(bones[i].name==item->contract.receiver_parent)parent=i;
					const int start=r.count;models.push_back({item->contract.model,start,item->bones});
					for(int n=0;n<item->bones;++n){r.weapon_bones[start+n]=true;r.parent[start+n]=n ? start : parent;bones[start+n].name=n ? "attachment_child" : item->contract.root;}
					if(item==&tavor::attachments[0])for(int n=0;n<2;++n)bones[start+n].name=tavor_fn2000_data::mars[n].name;
					if(!item->contract.muzzle.empty()){bones[start+1].name=item->contract.muzzle;bones[start+1].bind.position={6,0,0};}
					r.count+=item->bones;
				}
				for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
			}
			profile_match resolve() const{return select_profile(models,r,{bones.data(),size_t(r.count)});}
			physical_reload::part_rig parts(const reload_profile* d=nullptr) const{return physical_reload::bind_parts(r,{bones.data(),size_t(r.count)},d ? *d : *definition->reload);}
		};
		unsigned combinations{};
		for(const auto* d:{&tavor::assemblies[0],&tavor::assemblies[1],&fn2000::assemblies[0]})
		{
			const bool tar=d->id=="tavor";const auto& reload=*d->reload;
			for(bool silencer:{false,true})for(bool sensor:{false,true})for(int optic=-1;optic<(tar?11:13);++optic)
			for(bool reverse:{false,true})for(auto glove:{"viewhands_us_army","viewhands_arctic"})
			{
				std::vector<const assembly_attachment*> items;
				if(silencer)items.push_back(&rifle_attachments::common[0]);if(sensor)items.push_back(&rifle_attachments::common[14]);
				if(optic>=0)items.push_back(tar && optic==10 ? &tavor::attachments[0] : &rifle_attachments::common[1+optic]);
				if(reverse)std::reverse(items.begin(),items.end());fixture f(*d,items,glove);const auto match=f.resolve();const auto parts=f.parts();
				check(match.value==d && (match.muzzle>=0)==silencer,"TAR/FN2000 assembly preserves source skin, optic, sensor and muzzle identity");
				check(parts.valid && parts.magazine==(tar?72:75) && parts.slide==(tar?69:70) && parts.bullets==(tar?83:88),"TAR/FN2000 native action and rear magazine hierarchy binds");
				check(bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},*d).valid,"TAR/FN2000 complete fingers and authored moving parts bind");
				if(tar)check(!(parts.bullet_mask[84/32]&(0x80000000u>>(84%32))),"TAR follower never enters ammunition visibility mask");
				++combinations;
			}
			fixture bare(*d,{});
			check(!select_profile(bare.models,bare.r).value,"TAR/FN2000 cannot bind without bone topology");
			const assembly_attachment* duplicate[]{&rifle_attachments::common[5],&rifle_attachments::common[7]};
			check(!fixture(*d,duplicate).resolve().value,"TAR/FN2000 rejects competing optics");
			const assembly_attachment* shotgun[]{&scar::attachments[0]};check(!fixture(*d,shotgun).resolve().value,"unreviewed underbarrel cannot inherit a bare support pose");
			check(physical_reload::valid(reload.interaction) && !physical_reload::native_action_recoil(reload.interaction),"TAR/FN2000 action returns independently of firing");
			check(choose_part_grip(reload.slide_grips,reload.slide_grips[0].wrist,{},reload.slide_grab_low,reload.slide_grab_high,39.37007874f).distance_meters<.001f,"TAR/FN2000 native left grasp touches the actual left charging tab");
			for(auto gun:{anchor{{},{0,0,0,1}},anchor{{20,5,-4},normalize({.2f,-.3f,.1f,.8f})}})
			{
				const auto mag=compose_reload(gun,reload.magazine_rest);const auto tip=magazine_tip_in_well(reload,gun,physical_reload::translate_local(mag,magazine_exit_translation(reload,39.37007874f)),39.37007874f);
				check(tip[2]<-.0099f && std::abs(tip[0])<.001f && std::abs(tip[1])<.001f,"rear magazine clears authored mouth in rotated receiver frame");
			}
		}
		for(auto name:{"tavor_reflex","tavor_acog","tavor_mars","tavor_digital_acog","tavor_digital_eotech"})
			check(native_reload_profile(name,30,&tavor::physical)==&tavor::physical,"captured TAR native host names admit thirty-round mechanics");
		check(native_reload_profile("fn2000",30)==&fn2000::physical && !native_reload_profile("fn2000",20) && !native_reload_profile("tavor",30,&fn2000::physical),"FN2000 preserves its own native ammo and profile authority");
		const auto recipe=tavor::physical.magazine_mesh();
		check(recipe && recipe.count==3 && recipe.subsets==2 && recipe.selected_count(0)==2 && recipe.selected_count(1)==3 && recipe.selected_count(1000)==3 &&
			recipe.names[0]=="tag_clip" && recipe.names[1]=="j_plate" && recipe.names[2]=="j_bullet","TAR empty/full subset keeps follower as permanent structure");
		fixture tar(tavor::assemblies[0],{});auto invalid=tar;invalid.r.parent[84]=68;
		check(!invalid.parts().valid,"TAR follower must belong directly to magazine");
		for(auto name:{"j_bullet","j_reload","j_gun","tag_clip","missing"})
		{
			auto definition=tavor::physical;const std::string_view names[]{name};definition.magazine_body_bones=names;
			check(!tar.parts(&definition).valid,"structural magazine group cannot steal rounds, action, receiver or unknown geometry");
		}
		{
			auto definition=tavor::physical;const std::string_view names[]{"j_plate","j_plate"};definition.magazine_body_bones=names;
			check(!definition.magazine_mesh() && !tar.parts(&definition).valid,"duplicate permanent magazine groups fail closed");
			const std::string_view too_many[]{"a","b","c"};definition.magazine_body_bones=too_many;check(!definition.magazine_mesh(),"permanent magazine recipe is bounded");
		}
		for(auto* definition:reload_profiles)if(definition->magazine_body_bones.empty())
		{
			const auto legacy=definition->magazine_mesh();check(legacy && legacy.body_count==1 && legacy.subsets==definition->magazine_subset_count(),"existing weapon magazine subset ordering is preserved");
		}
		std::cout<<"TAR/FN2000 assembly combinations checked: "<<combinations<<'\n';return failed;
	}
}
