#pragma once
#include "component/vr/gameplay/hand_rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "scar_test_data.hpp"
#include "scar_foregrip_witness.hpp"
#include <iostream>
#include <vector>

namespace scar_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		int failed{};const auto check=[&](bool ok,const char* why){if(!ok){++failed;std::cerr<<"FAIL: "<<why<<'\n';}};
		{
			const auto resolved=resolve_rig(scar_foregrip_witness::models,scar_foregrip_witness::bones);
			const auto matched=select_profile(scar_foregrip_witness::models,resolved.layout,scar_foregrip_witness::bones);
			check(matched.value==&scar::foregrip,"actual native SCAR foregrip assembly is admitted");
			check(bind_weapon_poses(resolved.layout,scar_foregrip_witness::bones,scar::foregrip).valid&&
				physical_reload::bind_parts(resolved.layout,scar_foregrip_witness::bones,scar::physical).valid,
				"actual SCAR foregrip hands and rifle reload parts bind together");
		}
		struct fixture
		{
			rig r{};std::array<bone_definition,256> bones{};std::vector<model_definition> models;
			fixture(std::span<const assembly_attachment* const> items,std::string_view glove="viewhands_us_army")
			{
				r.parent.fill(-1);r.gun=68;r.count=87;r.parent[68]=13;r.arms[0].wrist=0;r.arms[1].wrist=1;
				models={{glove,0,68},{scar::bare.receiver,68,19}};
				for(size_t i=0;i<scar::idle_fingers.size();++i){bones[2+i].name=scar::idle_fingers[i].name;r.parent[2+i]=bones[2+i].name.find("_le")!=std::string_view::npos?0:1;}
				for(int n=0;n<19;++n){bones[68+n].name=scar_test_data::receiver[n].name;r.weapon_bones[68+n]=true;
					if(n)r.parent[68+n]=68+scar_test_data::receiver[n].parent;}
				for(const auto* item:items)
				{
					int parent=-1;for(int i=68;i<87;++i)if(bones[i].name==item->contract.receiver_parent)parent=i;
					const int start=r.count;models.push_back({item->contract.model,start,item->bones});
					std::span<const scar_test_data::bone> source;
					if(item->role==attachment_role::shotgun)source=scar_test_data::shotgun;
					if(item->role==attachment_role::launcher)source=scar_test_data::launcher;
					for(int n=0;n<item->bones;++n){r.weapon_bones[start+n]=true;
						bones[start+n].name=source.empty() ? (n ? "attachment_child" : item->contract.root) : source[n].name;
						r.parent[start+n]=n ? start+(source.empty() ? 0 : source[n].parent) : parent;}
					if(!item->contract.muzzle.empty()){bones[start+1].name=item->contract.muzzle;bones[start+1].bind.position={6,0,0};}
					r.count+=item->bones;
				}
				for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
			}
			profile_match resolve() const{return select_profile(models,r,{bones.data(),size_t(r.count)});}
			physical_reload::part_rig parts() const{return physical_reload::bind_parts(r,{bones.data(),size_t(r.count)},scar::physical);}
		};
		unsigned combinations{};
		for(int underbarrel=0;underbarrel<4;++underbarrel)for(bool silencer:{false,true})for(int optic=-1;optic<13;++optic)
		for(bool reverse:{false,true})for(auto glove:{"viewhands_us_army","viewhands_arctic"})
		{
			std::vector<const assembly_attachment*> items;
			if(underbarrel)items.push_back(&scar::attachments[underbarrel-1]);
			if(silencer)items.push_back(&rifle_attachments::common[0]);
			if(optic>=0)items.push_back(&rifle_attachments::common[1+optic]);
			if(reverse)std::reverse(items.begin(),items.end());
			fixture f(items,glove);const auto match=f.resolve();const auto parts=f.parts();
			const auto* expected=underbarrel==1 ? &scar::shotgun : underbarrel==2 ? &scar::grenadier : underbarrel==3 ? &scar::foregrip : &scar::bare;
			check(match.value==expected && expected->reload==&scar::physical && match.hidden==part_mask{},"SCAR complete assembly preserves visible attachments and host mechanics");
			check((match.muzzle>=0)==silencer && bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},*expected).valid,"SCAR native finger and receiver poses bind in every assembly order");
			check(parts.valid && parts.slap_hand.valid && parts.right_slap_hand.valid && parts.magazine==74 && parts.slide==70 && parts.bullets==86,"SCAR selects rifle parts and both slap hands without selecting underbarrel parts");
			for(int i=0;i<f.r.count;++i)check(bool(parts.bullet_mask[i/32]&(0x80000000u>>(i%32)))==(i==86),"SCAR rifle bullet visibility never hides shotgun shell or launcher grenade");
			check(parts.animation_only_magazines==part_mask{},"SCAR has no animation-only magazine copy to hide");
			++combinations;
		}
		fixture f({});
		for(auto name:{"scar_h","scar_h_acog","scar_h_reflex","scar_h_thermal","scar_h_shotgun","scar_h_grenadier","scar_h_fgrip"})
			check(native_reload_profile(name,20,&scar::physical)==&scar::physical,"captured SCAR host identities bind twenty-round mechanics");
		for(auto name:{"scar_h_shotgun_attach","scar_h_shotgun_attach_reflex","scar_h_m203","scar_h_m203_acog","scar_h_","scar_h2","scar_h/invalid"})
			check(!native_reload_profile(name,20,&scar::physical),"SCAR alternate and malformed identities fail even with forged rifle capacity");
		check(!native_reload_profile("scar_h_shotgun",4) && !scar::native_family(std::string(10000,'x')),"SCAR validates capacity and bounded native names");
		for(auto items:{std::vector<const assembly_attachment*>{&scar::attachments[0],&scar::attachments[0]},
			std::vector<const assembly_attachment*>{&scar::attachments[0],&scar::attachments[1]}})
			check(!fixture(items).resolve().value,"SCAR rejects duplicate or competing underbarrels");
		const assembly_attachment* shot[]{&scar::attachments[0]};fixture bad(shot);bad.r.parent[87]=0;
		check(!bad.resolve().value,"SCAR shotgun root must attach to its receiver tag");
		bad=fixture(shot);bad.r.parent[86]=87;check(!bad.parts().valid,"SCAR rounds cannot be parented to the shotgun");
		bad=fixture(shot);bad.bones[88].name="tag_clip";check(!bad.parts().valid,"ambiguous underbarrel magazine names fail reload admission");
		check(!select_profile(f.models,f.r).value,"SCAR requires bone topology, not names alone");
		for (const auto* competing:{&scar::attachments[0],&scar::attachments[1],&scar::attachments[2]})
		{
			const assembly_attachment* items[]{&scar::attachments[2],competing};
			check(!fixture(items).resolve().value,"SCAR foregrip excludes every competing support and duplicate grip");
		}
		const assembly_attachment* grip[]{&scar::attachments[2]};fixture grip_bad(grip);grip_bad.r.parent[87]=68;
		check(!grip_bad.resolve().value,"SCAR foregrip must attach to the actual receiver grip tag");
		check(scar::foregrip.wrists[1].position==scar::bare.wrists[1].position &&
			free_hand_rotation(scar::foregrip,0)==free_hand_rotation(scar::bare,0) &&
			scar::foregrip.wrists[0].position!=scar::bare.wrists[0].position &&
			scar::foregrip.fingers[0].rotation!=scar::bare.fingers[0].rotation,
			"SCAR vertical grip owns native support contact and fingers while retaining control/free hand bases");
		check(scar::suppress_equip("h2_wpn_asl_scar_h_fgrip_pullout") &&
			!scar::suppress_equip("h2_wpn_asl_scar_h_fgrip_reload"),"foregrip equip does not suppress native reload actions");
		check(scar::bare.wrists[1].position==scar::shotgun.wrists[1].position && scar::bare.wrists[0].position!=scar::shotgun.wrists[0].position &&
			free_hand_rotation(scar::bare,0)==free_hand_rotation(scar::shotgun,0),"SCAR shotgun alters only support contact and retains free-hand basis");
		check(scar::suppress_equip("h2_wpn_asl_scar_h_shotgun_pullout") && !scar::suppress_equip("h2_wpn_asl_scar_h_shotgun_shotty_pullout"),"SCAR equip suppression cannot own shotgun-mode actions");
		const auto& d=scar::physical;
		check(physical_reload::valid(d.interaction) && physical_reload::native_action_recoil(d.interaction),"SCAR action retains native reciprocation and measured hold-open");
		check(choose_part_grip(d.slide_grips,d.slide_grips[0].wrist,{},d.slide_grab_low,d.slide_grab_high,39.37007874f).distance_meters<.001f,"SCAR native left hand contacts real left charging tab");
		for(auto gun:{anchor{{},{0,0,0,1}},anchor{{20,5,-4},normalize({.2f,-.3f,.1f,.8f})}})
		{
			const auto mag=compose_reload(gun,d.magazine_rest);
			const auto tip=magazine_tip_in_well(d,gun,physical_reload::translate_local(mag,magazine_exit_translation(d,39.37007874f)),39.37007874f);
			check(tip[2]<-.0099f && std::abs(tip[0])<.001f && std::abs(tip[1])<.001f,"SCAR ejected magazine clears reviewed mouth in rotated gun frames");
		}
		std::cout<<"SCAR assembly combinations checked: "<<combinations<<'\n';return failed;
	}
}
