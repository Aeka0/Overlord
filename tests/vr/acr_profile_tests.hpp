#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/hand_pose_library.hpp"
#include "component/vr/gameplay/rigid_part_visibility.hpp"
#include <iostream>
#include <vector>
#include "acr_arctic_data.hpp"

namespace acr_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		int failed{}; const auto check=[&](bool ok,const char* why) { if (!ok) { ++failed; std::cerr<<"FAIL: "<<why<<'\n'; } };
		struct fixture
		{
			rig r{}; std::array<bone_definition,256> bones{}; std::vector<model_definition> models;
			fixture(size_t skin,std::span<const assembly_attachment* const> items,std::string_view glove="viewhands_us_army")
			{
				r.parent.fill(-1); r.gun=68; r.count=87; r.parent[68]=13;
				models={{glove,0,68},{acr::skins[skin]->rigid_magazine_source,68,19}};
				constexpr std::string_view names[]{"j_gun","j_bolt","j_trigger","tag_acog_2","tag_brass","tag_clip",
					"tag_eotech","tag_flash","tag_front_sight_off","tag_front_sight_on","tag_heartbeat","tag_m203",
					"tag_rear_sight","tag_red_dot","tag_shotgun","tag_silencer","tag_thermal_scope","j_bullets","tag_bullet"};
				for (int i=68;i<87;++i) { bones[i].name=names[i-68]; r.weapon_bones[i]=true; if (i>68) r.parent[i]=68; }
				r.parent[85]=r.parent[86]=73;
				for (const auto* item:items)
				{
					int parent=-1; for (int i=68;i<87;++i) if (bones[i].name==item->contract.receiver_parent) parent=i;
					const auto start=r.count; models.push_back({item->contract.model,start,item->bones});
					for (int n=0;n<item->bones;++n) { r.weapon_bones[start+n]=true; r.parent[start+n]=n ? start : parent; }
					bones[start].name=item->contract.root;
					if (!item->contract.muzzle.empty()) { bones[start+1].name=item->contract.muzzle; bones[start+1].bind.position={6.68582f,0,0}; }
					r.count+=item->bones;
				}
				for (int i=0;i<r.count;++i) { bones[i].bind.rotation={0,0,0,1}; bones[i].parent=r.parent[i]; }
			}
			profile_match resolve() const { return select_profile(models,r,{bones.data(),size_t(r.count)}); }
		};
		{
			const int skin=acr::receiver_skin("h2_viewmodel_magpul_masada_base_arctic");
			check(skin>=0,"live arctic ACR receiver is registered");
			if(skin>=0)
			{
				const assembly_attachment sensor{{"attach_h2_heartbeat_vm_arctic","tag_heartbeat","tag_heartbeat"},attachment_role::sensor,9};
				const assembly_attachment* items[]{&rifle_attachments::common[8],&rifle_attachments::common[0],&sensor};
				fixture f(skin,items,"viewhands_arctic");
				const auto apply=[&](const auto& source,int start,int root) {for(size_t i=0;i<source.size();++i)
				{f.bones[start+i].name=source[i].name;f.bones[start+i].parent=f.r.parent[start+i]=source[i].parent<0 ? root : start+source[i].parent;}};
				apply(acr_arctic_data::receiver,68,13);apply(acr_arctic_data::sensor,91,78);
				const auto selected=f.resolve();
				check(selected.value && selected.value->reload==&acr::arctic && selected.value->variant=="bare" && selected.muzzle==90,
					"captured arctic ACR, red dot, suppressor and heartbeat select the common rifle actions");
				check(physical_reload::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},acr::arctic).valid &&
					native_reload_profile("masada_silencer_mt_camo_on_h2",30,&acr::arctic)==&acr::arctic,
					"captured ACR retains physical magazine/handle binding and its own camouflage");
				f.r.parent[91]=68;check(!f.resolve().value,"arctic heartbeat requires the receiver attachment tag");
				f.r.parent[91]=78;f.models.back().name="attach_h2_heartbeat_vm_unreviewed";
				check(!f.resolve().value,"reviewed heartbeat camouflage does not admit arbitrary aliases");
				const assembly_attachment* duplicates[]{&rifle_attachments::common[14],&sensor};
				check(!fixture(skin,duplicates).resolve().value,"base and arctic sensors cannot bypass one-sensor cardinality");
			}
		}
		unsigned combinations{};
		for (size_t skin=0;skin<acr::skins.size();++skin) for (int launcher=-1;launcher<int(acr::launchers.size());++launcher) for (bool silencer:{false,true})
		for (int optic=-1;optic<13;++optic) for (int sensor=-1;sensor<2;++sensor) for (bool reverse:{false,true})
		for (auto glove:{"viewhands_us_army","viewhands_arctic","viewhands_other"})
		{
			const bool gl=launcher>=0;
			std::vector<const assembly_attachment*> items;
			if (gl) items.push_back(&acr::launchers[launcher]); if (silencer) items.push_back(&rifle_attachments::common[0]);
			if (optic>=0) items.push_back(&rifle_attachments::common[1+optic]); if (sensor>=0) items.push_back(&rifle_attachments::common[sensor ? 16 : 14]);
			if (reverse) std::reverse(items.begin(),items.end());
			fixture f(skin,items,glove); const auto match=f.resolve(); const auto* expected=&acr::assemblies[skin*2+int(gl)];
			check(match.value==expected && match.value->reload==acr::skins[skin],"ACR assembly chooses only support variant and exact receiver magazine skin");
			check((match.muzzle>=0)==silencer,"ACR suppressor retains independent muzzle mapping");
			const auto parts=physical_reload::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},*acr::skins[skin]);
			check(parts.valid && parts.slide==69 && parts.magazine==73 && parts.bullets==86 && parts.bolt==-1,
				"all ACR combinations retain the actual side handle and two magazine-child round groups");
			check(native_reload_profile("masada_grenadier",30,expected->reload)==expected->reload,"ACR native family and exact scene skin agree without merging instance names");
			++combinations;
		}
		fixture bare(0,{}); check(bare.resolve().value==&acr::assemblies[0],"ACR bare receiver needs no fictional foregrip attachment");
		check(!select_profile(bare.models,bare.r).value,"ACR family requires bone topology, not just a receiver name");
		const assembly_attachment* launcher[]{&acr::launchers[0]}; fixture gl(1,launcher);
		gl.r.parent[87]=0; check(!gl.resolve().value,"M203 cannot be parented to the hand");
		gl.r.parent[87]=79; gl.models.back().name="unreviewed_launcher";
		check(!gl.resolve().value,"unknown underbarrel cannot silently use the M203 grip");
		const assembly_attachment* duplicate[]{launcher[0],launcher[0]};
		check(!fixture(0,duplicate).resolve().value,"duplicate launchers reject before indexing ACR support variants");
		const assembly_attachment* mixed_launchers[]{&acr::launchers[0],&acr::launchers[1]};
		check(!fixture(2,mixed_launchers).resolve().value,"different M203 skins still count as duplicate launchers");
		const assembly_attachment* observed[]{&rifle_attachments::common[6],&acr::launchers[1]};
		fixture digital(2,observed,"viewhands_tf141"); const auto captured=digital.resolve();
		check(digital.models[2].name=="attach_h2_eotech_2_vm_digital" && digital.models[3].name=="attach_h2_m203_vm_digital" &&
			digital.r.parent[87]==74 && digital.r.parent[89]==79 && captured.value==&acr::assemblies[5] &&
			native_reload_profile("masada_digital_grenadier_eotech",30,captured.value->reload)==acr::skins[2],
			"observed digital ACR with EOTech and M203 admits grip and exact digital rifle magazine together");
		check(!native_reload_profile("gl_masada_digital_eotech",1,acr::skins[2]),
			"observed linked grenade feed never inherits rifle reload authority");
		const assembly_attachment* laser[]{&rifle_attachments::common[15]};
		check(!fixture(0,laser).resolve().value,"shared laser alias cannot fabricate ACR's missing tag_laser");
		const assembly_attachment* shotgun[]{&ak47::attachments[1]};
		check(!fixture(0,shotgun).resolve().value,"presence of tag_shotgun does not authorize an unreviewed shotgun variant");
		bare.bones[85].name="missing_lower_rounds";
		check(!physical_reload::bind_parts(bare.r,{bare.bones.data(),size_t(bare.r.count)},acr::physical).valid,
			"lower magazine stack is part of complete ACR admission");
		fixture fresh(0,{}); const auto parts=physical_reload::bind_parts(fresh.r,{fresh.bones.data(),size_t(fresh.r.count)},acr::physical);
		fresh.r.arms[0].wrist=0; fresh.r.arms[1].wrist=1;
		for (size_t i=0;i<acr::grenadier_fingers.size();++i)
		{
			fresh.bones[2+i].name=acr::grenadier_fingers[i].name;
			fresh.r.parent[2+i]=acr::grenadier_fingers[i].name.find("_le")!=std::string_view::npos ? 0 : 1;
		}
		for (const auto& profile:acr::assemblies)
			check(bind_weapon_poses(fresh.r,{fresh.bones.data(),size_t(fresh.r.count)},profile).valid,
				"ACR live glove fingers and all five parent-local equip parts bind for every support and skin variant");
		part_mask hidden=parts.bullet_mask; hidden[73/32]|=0x80000000u>>(73%32);
		const std::array<rigid_group_range,3> groups{{{5*64,2737,0,2228},{17*64,1124,2228,1528},{18*64,434,3756,594}}};
		check(plan_rigid_visibility(groups,68,4295,4350,parts.bullet_mask).hidden_groups==6 &&
			plan_rigid_visibility(groups,68,4295,4350,hidden).hidden_groups==7,"ACR magazine surface hides body, top round and lower stack independently");
		const std::array<rigid_group_range,1> handle{{{64,168,0,224}}};
		check(plan_rigid_visibility(handle,68,168,224,hidden).hidden_groups==0,"magazine removal cannot hide the ACR side handle");
		for (auto name:{"masada","masada_grenadier","masada_silencer","masada_heartbeat_digital","masada_digital_grenadier_eotech"})
			check(native_reload_profile(name,30)==&acr::physical,"ACR native family discovery uses common ammunition data");
		for (auto name:{"acr","masada_","masada2","masada/invalid","m203_masada","MASADA","m4"})
			check(!acr::native_family(name),"unreviewed native identity or launcher feed cannot enter ACR rifle authority");
		check(!acr::native_family(std::string(10000,'a')) && !native_reload_profile("masada",31) && !native_reload_profile("masada",1),
			"ACR names and base capacity are bounded before native admission");
		for (size_t skin=0;skin<acr::skins.size();++skin)
		{
			const auto& a=acr::assemblies[skin*2]; const auto& b=acr::assemblies[skin*2+1];
			check(a.aiming==aim_rule::two_hand && b.aiming==aim_rule::two_hand && a.wrists[1].position==b.wrists[1].position &&
				a.wrists[1].rotation==b.wrists[1].rotation && a.wrists[0].position!=b.wrists[0].position && free_hand_rotation(a,0)==free_hand_rotation(b,0),
				"M203 changes the support contact while retaining rear ownership and free-hand magazine/handle basis");
			for (size_t i=0;i<acr::idle_fingers.size();++i) if (acr::idle_fingers[i].name.find("_ri_")!=std::string_view::npos || acr::idle_fingers[i].name.ends_with("_ri"))
				check(a.fingers[i].rotation==b.fingers[i].rotation,"ACR M203 support does not replace the native rear fingers");
			check(a.reload->ammunition.plus_one && a.reload->ammunition.last_round_lock && a.reload->ammunition.release_control,
				"all ACR skins preserve M4-style chamber and empty-lock controls");
			const auto span=sub(a.wrists[0].position,a.wrists[1].position);
			for (auto direction:{vec{0,1,0},vec{0,0,1},vec{-1,0,0}})
				check(length(sub(rotate(aimed_rotation(a.aiming,{0,0,0,1},{},scale(direction,length(span)),span),span),scale(direction,length(span))))<.001f,
					"ACR native grip baseline handles vertical and reversed two-hand aim");
		}
		const auto& p=acr::physical;
		check(physical_reload::valid(p.interaction) && p.interaction.locked_travel==0 && !physical_reload::native_action_recoil(p.interaction) &&
			p.interaction.slide_stroke>.112f,"ACR uses its full native side-handle stroke, with forward rest on empty lock");
		check(acr::idle_fingers.size()==36 && p.magazine_fingers.size()==18 && p.slide_grips[0].fingers.size()==18,"ACR has complete authored native and manipulation palm/finger chains");
		check(acr::launcher_fingers.size()==18 && acr::grenadier_fingers.size()==36 &&
			length(sub(scale(acr::launcher_support.position,2.54f),{26.850746f,4.674501f,-.946871f}))<.0001f,
			"ACR launcher support uses the captured native GL wrist and complete left palm chain");
		for (const auto& joint:acr::launcher_fingers)
			check(std::count_if(acr::grenadier_fingers.begin(),acr::grenadier_fingers.end(),[&](const auto& pose) {
				return pose.name==joint.name && pose.rotation==joint.rotation;
			})==1,"native launcher finger and palm overrides bind exactly once");
		for (const auto& profile:acr::assemblies)
		{
			const auto library=bind_weapon_poses(fresh.r,{fresh.bones.data(),size_t(fresh.r.count)},profile);
			for (auto pose:{p.magazine_fingers,p.slide_grips[0].fingers}) for (const auto& joint:pose)
			{
				const auto bone=std::find_if(fresh.bones.begin(),fresh.bones.begin()+fresh.r.count,[&](const auto& b){return b.name==joint.name;});
				check(bone!=fresh.bones.begin()+fresh.r.count && library.finger[bone-fresh.bones.begin()]>=0,
					"magazine and handle palms can override either bare or launcher support through the same pose library");
			}
		}
		for (const auto& finger:acr::grenadier_fingers)
		{ float norm{}; for (float x:finger.rotation) norm+=x*x; check(std::isfinite(norm) && std::abs(norm-1)<.001f,"ACR authored rotations are normalized"); }
		for (auto clip:{"h2_wpn_asl_masada_pullout_first","h2_wpn_asl_masada_gl_pullout","h2_wpn_asl_masada_gl_putaway_quick",
			"h2_wpn_asl_masada_hb_open_pullout","h2_wpn_asl_masada_hb_close_putaway"})
			check(acr::suppress_equip(clip),"bare and launcher-equipped rifle equip clips preserve physical hand/part poses");
		for (auto clip:{"h2_wpn_asl_masada_gl_reload","h2_wpn_asl_masada_gl_fire","h2_wpn_asl_masada_gl_grenade_pullout","h2_wpn_asl_m4a1_gl_pullout",
			"h2_wpn_asl_masada_hb_close2open","h2_wpn_asl_masada_hb_open2close","h2_wpn_asl_masada_hb_open_reload"})
			check(!acr::suppress_equip(clip),"ACR equip policy excludes grenade operation, reload, fire and foreign clips");
		check(choose_part_grip(p.slide_grips,p.slide_grips[0].wrist,{},p.slide_grab_low,p.slide_grab_high,39.37007874f).distance_meters<.001f,
			"ACR overhand contact is inside the actual moving side handle region");
		for (auto gun:{anchor{{},{0,0,0,1}},anchor{{200,-50,12},normalize({.2f,-.4f,.1f,.8f})}})
		{
			const auto seated=compose_reload(gun,p.magazine_rest);
			check(magazine_alignment(p,gun,seated)>.999f,"ACR magazine contact follows translated and rotated receiver");
			const auto exited=physical_reload::translate_local(seated,magazine_exit_translation(p,39.37007874f));
			const auto tip=magazine_tip_in_well(p,gun,exited,39.37007874f);
			check(tip[2]<=-.0099f && std::abs(tip[0])<.001f && std::abs(tip[1])<.001f,"ACR magazine fully clears its real receiver mouth before free drop");
		}
		check(!p.sound_key(mechanics::effect::shot) && !p.sound_key(mechanics::effect::action_rear),"ACR compound handle audio and native firing are not doubled");
		std::cout<<"ACR assembly combinations checked: "<<combinations<<'\n';
		return failed;
	}
}
