#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/aa12/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "aa12_data.hpp"
#include <iostream>
#include <vector>

namespace aa12_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		int failed{};const auto check=[&](bool ok,const char* why){if(!ok){++failed;std::cerr<<"FAIL: "<<why<<'\n';}};
		struct fixture
		{
			rig r{};std::array<bone_definition,256> bones{};std::vector<model_definition> models;
			fixture(std::span<const assembly_attachment* const> items={},std::string_view glove="viewhands_us_army")
			{
				r.parent.fill(-1);r.gun=68;r.parent[68]=13;r.arms[0].wrist=0;r.arms[1].wrist=1;
				r.count=82;models={{glove,0,68},{aa12::base.receiver,68,14}};
				for(size_t i=0;i<aa12::base.fingers.size();++i){bones[2+i].name=aa12::base.fingers[i].name;r.parent[2+i]=0;}
				for(size_t i=0;i<aa12_data::receiver.size();++i)
				{
					bones[68+i].name=aa12_data::receiver[i].name;r.weapon_bones[68+i]=true;
					if(i)r.parent[68+i]=68+aa12_data::receiver[i].parent;
				}
				for(const auto* item:items)
				{
					int parent=-1;for(int i=68;i<82;++i)if(bones[i].name==item->contract.receiver_parent)parent=i;
					const int start=r.count;models.push_back({item->contract.model,start,item->bones});
					for(int n=0;n<item->bones;++n){r.weapon_bones[start+n]=true;r.parent[start+n]=n ? start : parent;bones[start+n].name=n ? "attachment_child" : item->contract.root;}
					if(!item->contract.muzzle.empty()){bones[start+1].name=item->contract.muzzle;bones[start+1].bind.position={6,0,0};}
					r.count+=item->bones;
				}
				for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
			}
			profile_match resolve() const{return select_profile(models,r,{bones.data(),size_t(r.count)});}
			physical_reload::part_rig parts() const{return physical_reload::bind_parts(r,{bones.data(),size_t(r.count)},aa12::physical);}
		};
		unsigned combinations{};
		// These optic models share source-reviewed mounts; unavailable receiver
		// mounts still fail closed. Native iron-sight/rail hide masks stay native.
		for(int optic:{-1,5,6,7,8,9,10})for(bool silencer:{false,true})for(bool sensor:{false,true})
		for(bool reverse:{false,true})for(auto glove:{"viewhands_us_army","viewhands_arctic"})
		{
			std::vector<const assembly_attachment*> items;
			if(optic>=0)items.push_back(&rifle_attachments::common[optic]);
			if(silencer)items.push_back(&rifle_attachments::common[0]);if(sensor)items.push_back(&rifle_attachments::common[14]);
			if(reverse)std::reverse(items.begin(),items.end());fixture f(items,glove);const auto match=f.resolve();const auto parts=f.parts();
			check(match.value==&aa12::base && (match.muzzle>=0)==silencer,"AA-12 complete receiver/optic/muzzle assembly binds");
			check(parts.valid && parts.magazine==71 && parts.slide==69 && parts.bullets==80,"AA-12 magazine, action and shell retain native identities");
			check(bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},aa12::base).valid,"AA-12 authored fingers and child handle rest bind");
			check(!(parts.bullet_mask[81/32]&(0x80000000u>>(81%32))),"AA-12 top handle is never hidden with empty magazine shells");
			++combinations;
		}
		fixture bare;check(!select_profile(bare.models,bare.r).value,"AA-12 requires bone topology");
		for(int parent:{-1,68,71,80}){auto f=bare;f.r.parent[81]=parent;check(!f.resolve().value,"AA-12 rejects detached or wrongly parented handle");}
		{auto f=bare;f.bones[81].name="j_reload";check(!f.resolve().value,"AA-12 duplicate action cannot substitute for handle");}
		{auto f=bare;f.r.parent[80]=68;check(!f.parts().valid,"AA-12 magazine shell must remain magazine-owned");}
		for(int optic:{1,11,15}){const assembly_attachment* items[]{&rifle_attachments::common[optic]};check(!fixture(items).resolve().value,"AA-12 rejects attachments without matching receiver mount");}
		const assembly_attachment* duplicate[]{&rifle_attachments::common[5],&rifle_attachments::common[7]};
		check(!fixture(duplicate).resolve().value,"AA-12 competing optics fail closed");
		const assembly_attachment* shotgun[]{&scar::attachments[0]};check(!fixture(shotgun).resolve().value,"AA-12 does not borrow underbarrel rifle pose");
		for(auto name:{"aa12","aa12_reflex"})check(native_reload_profile(name,8)==&aa12::physical,"captured AA-12 variants use eight-shell feed");
		for(auto name:{"aa12","aa12_reflex"})
		{
			check(native_reload_shape_supported(name,8,false,8),"captured AA-12 reloadAmmoAdd eight admits whole magazine replacement");
			check(native_reload_shape_supported(name,8,false,0),"native default replacement remains supported");
			for(int add:{-1,1,7,9,1000000})check(!native_reload_shape_supported(name,8,false,add),"partial, negative and excessive native additions fail closed");
			check(!native_reload_shape_supported(name,8,true,8),"segmented AA-12 descriptor cannot borrow detachable-magazine logic");
		}
		check(!native_reload_shape_supported("striker",12,false,12) && !native_reload_shape_supported("aa12",20,false,20),"unregistered full-magazine values cannot bypass admission");
		check(native_reload_shape_supported("magnum44",6,false,0),"existing zero-add cylinder observation remains available");
		for(auto name:{"aa120","striker","m1014","scar_h_shotgun_attach"})check(!aa12::physical.matches_native(name,8),"AA-12 family admission excludes other shotguns");
		check(!native_reload_profile("aa12",20) && !native_reload_profile("aa12",8,&scar::physical),"AA-12 cannot inherit another magazine capacity or instance profile");
		const auto& d=aa12::physical;
		check(d.ammunition.feed==mechanics::feed_type::closed_bolt && !d.ammunition.last_round_lock && !d.ammunition.release_control &&
			d.ammunition.plus_one && physical_reload::valid(d.interaction) && physical_reload::native_action_recoil(d.interaction),"AA-12 explicit game closed-bolt policy preserves reciprocating action");
		check(choose_part_grip(d.slide_grips,d.slide_grips[0].wrist,{},d.slide_grab_low,d.slide_grab_high,39.37007874f).distance_meters<.001f,"AA-12 hand contact is on the exposed top handle");
		for(auto gun:{anchor{{},{0,0,0,1}},anchor{{20,5,-4},normalize({.2f,-.3f,.1f,.8f})}})
		{
			const auto mag=compose_reload(gun,d.magazine_rest);const auto tip=magazine_tip_in_well(d,gun,physical_reload::translate_local(mag,magazine_exit_translation(d,39.37007874f)),39.37007874f);
			check(tip[2]<-.0099f && std::abs(tip[0])<.001f && std::abs(tip[1])<.001f,"AA-12 dropped magazine clears receiver mouth in rotated coordinates");
		}
		const auto mesh=d.magazine_mesh();check(mesh && mesh.selected_count(0)==1 && mesh.selected_count(8)==2 && mesh.names[1]=="j_bullet","AA-12 empty magazine excludes shell geometry");
		check(aa12::suppress_equip("h2_wpn_sho_aa12_first_pullout") && !aa12::suppress_equip("h2_wpn_sho_aa12_fire"),"AA-12 equip suppression preserves fire animation");
		std::cout<<"AA-12 assembly combinations checked: "<<combinations<<'\n';return failed;
	}
}
