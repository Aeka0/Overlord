#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/underbarrel_rig.hpp"
#include "m4_arctic_data.hpp"
#include <vector>
#include <iostream>

namespace m4_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		int failed{}; const auto check=[&](bool v,const char* why) { if (!v) { ++failed; std::cerr<<"FAIL: "<<why<<'\n'; } };
		struct fixture
		{
			rig r{}; std::array<bone_definition,256> bones{}; std::vector<model_definition> models;
			fixture(std::span<const m4::attachment* const> items,std::string_view glove)
			{
				r.parent.fill(-1); r.gun=68; r.count=92; r.parent[68]=13;
				r.arms[0].wrist=0;r.arms[1].wrist=1;
				for(size_t i=0;i<m4::idle_fingers.size();++i)
				{bones[2+i].name=m4::idle_fingers[i].name;r.parent[2+i]=bones[2+i].name.find("_le")!=std::string_view::npos?0:1;}
				models={{glove,0,68},{m4::foregrip.receiver,68,24}};
				constexpr std::string_view names[]{"j_gun","j_clip_release","j_flip","j_reload","j_trigger",
					"tag_acog_2","tag_brass","tag_clip","tag_cover","tag_eotech","tag_flash","tag_foregrip",
					"tag_heartbeat","tag_laser","tag_m203","tag_magnifier","tag_red_dot","tag_shotgun",
					"tag_sight_off","tag_sight_on","tag_silencer","tag_thermal_scope","j_bullet","j_reload_trigger"};
				for (int i=68;i<92;++i) { bones[i].name=names[i-68]; r.weapon_bones[i]=true; if (i>68) r.parent[i]=68; }
				r.parent[90]=75; r.parent[91]=71;
				for (const auto* item:items)
				{
					int parent=-1; for (int i=68;i<92;++i) if (bones[i].name==item->contract.receiver_parent) parent=i;
					const auto start=r.count;
					models.push_back({item->contract.model,start,item->bones});
					for (int n=0;n<item->bones;++n) { r.weapon_bones[start+n]=true; r.parent[start+n]=n ? start : parent; }
					bones[start].name=item->contract.root;
					if (!item->contract.muzzle.empty()) { bones[start+1].name=item->contract.muzzle; bones[start+1].bind.position={6.68582f,0,0}; }
					r.count+=item->bones;
				}
				for (int i=0;i<r.count;++i) { bones[i].bind.rotation={0,0,0,1}; bones[i].parent=r.parent[i]; }
			}
			profile_match resolve() const { return select_profile(models,r,{bones.data(),size_t(r.count)}); }
		};
		unsigned combinations{};
		{
			const m4::attachment* items[]{&m4::attachments[19],&m4::attachments[1],&m4::attachments[12]};
			fixture captured(items,"viewhands_udt");
			check(captured.models.size()==m4_arctic_data::models.size()+1,"capture includes receiver, laser, M203 and arctic reflex");
			for (size_t m=0;m<m4_arctic_data::models.size();++m)
			{
				const auto& source=m4_arctic_data::models[m];auto& model=captured.models[m+1];
				check(model.count==int(source.bones.size()),"captured model bone counts agree with the attachment contract");
				model.name=source.name;
				const int parent=captured.r.parent[model.begin];
				for (size_t i=0;i<source.bones.size();++i)
				{
					const int index=model.begin+int(i);auto bone=source.bones[i];
					bone.parent=bone.parent<0 ? parent : model.begin+bone.parent;
					if (m)
					{
						const auto posed=vr::gameplay::hands::pose_math::compose(vr::gameplay::hands::pose_math::as_anchor(captured.bones[parent].bind),vr::gameplay::hands::pose_math::as_anchor(bone.bind));
						bone.bind.position=posed.position;bone.bind.rotation=posed.rotation;
					}
					captured.bones[index]=bone;captured.r.parent[index]=bone.parent;
				}
			}
			const auto bones=std::span<const bone_definition>(captured.bones.data(),captured.r.count);
			check(captured.resolve().value==&m4::arctic_grenadier,"live Gulag assembly selects snow M4 support and receiver");
			check(bind_weapon_poses(captured.r,bones,m4::arctic_grenadier).valid &&
				physical_reload::bind_parts(captured.r,bones,m4::arctic_physical).valid,
				"captured arctic hierarchy binds shared hands, magazine and charging handle");
			check(bool(underbarrel::bind(captured.models,captured.r,bones)),"captured Gulag M203 binds its separate physical module");
			check(native_reload_profile("m4m203_reflex_arctic",30,&m4::arctic_physical)==&m4::arctic_physical &&
				std::string_view(m4::arctic_physical.skinned_receiver)==captured.models[1].name,
				"arctic scene admission prepares its own skinned visibility instead of the base asset");
			check(!native_reload_profile("m203_m4_reflex_arctic",1,&m4::arctic_physical) &&
				!native_reload_profile("m4m203_reflex_arctic",31,&m4::arctic_physical),
				"arctic registration preserves launcher-feed and capacity boundaries");
			captured.r.parent[captured.models[3].begin]=0;
			check(!captured.resolve().value,"arctic model name cannot bypass M203 parent validation");
			captured.models[1].name="h2_viewmodel_m4_base_unreviewed";
			check(!captured.resolve().value,"new snow recipe does not admit arbitrary receiver suffixes");
		}
		for (auto glove:{"viewhands_us_army","viewhands_arctic","viewhands_test_different_glove"})
		for (int under=0;under<2;++under) for (int cover=-1;cover<2;++cover)
		for (bool silencer:{false,true}) for (int optic=-1;optic<13;++optic) for (bool reverse:{false,true})
		for (unsigned extra=0;extra<4;++extra)
		{
			std::vector<const m4::attachment*> items{&m4::attachments[under]};
			if (cover>=0) items.push_back(&m4::attachments[2+cover]);
			if (silencer) items.push_back(&m4::attachments[4]);
			if (optic>=0) items.push_back(&m4::attachments[5+optic]);
			if (extra&1) items.push_back(&m4::attachments[18]);
			if (extra&2) items.push_back(&m4::attachments[19]);
			if (reverse) std::reverse(items.begin(),items.end());
			fixture f(items,glove); const auto resolved=f.resolve();
			check(resolved.value==(under ? &m4::grenadier : &m4::foregrip),"M4 combination selects only the underbarrel's support pose");
			check((resolved.muzzle>=0)==silencer,"M4 suppressor independently supplies the muzzle");
			const auto parts=physical_reload::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},m4::physical);
			check(parts.valid && parts.slap_hand.valid && parts.right_slap_hand.valid,"M4 receiver and both slap hands survive attachment order/glove/optic changes");
			check(resolved.value && resolved.value->reload==&m4::physical,"all M4 combinations share one reload definition");
			++combinations;
		}
		const m4::attachment* one[]{&m4::attachments[0]}; fixture valid(one,"viewhands_us_army");
		check(!select_profile(valid.models,valid.r).value,"M4 no longer accepts names without topology");
		valid.r.parent[92]=0; check(!valid.resolve().value,"M4 underbarrel cannot attach to a hand");
		valid.r.parent[92]=79; valid.models.back().name="attach_h2_unknown_vm";
		check(!valid.resolve().value,"unknown weapon-owned attachment rejected");
		const m4::attachment* both[]{&m4::attachments[0],&m4::attachments[1]};
		check(!fixture(both,"viewhands_us_army").resolve().value,"M4 foregrip and M203 mutually exclusive");
		const m4::attachment* duplicate[]{one[0],one[0]};
		check(!fixture(duplicate,"viewhands_us_army").resolve().value,"duplicate underbarrel rejected");
		check(!fixture({},"viewhands_us_army").resolve().value,"M4 requires an underbarrel");
		for (auto name:{"m4_silencer","m4_grenadier_airport","m4","m4m203","m4m203_eotech"})
			check(native_reload_profile(name,30)==&m4::physical,"M4 family resolves common physical data");
		for (auto name:{"m40a3","m203_m4_airport","m203_m4_eotech","m4a1","m4/invalid","ak47",
			"m4m2030","m4m203_","m4m203/invalid","m4m203_eotech!"})
			check(native_reload_profile(name,30)!=&m4::physical,"non-M4 or launcher identity cannot use M4 rifle feed");
		check(!native_reload_profile("m4_silencer",1) && !native_reload_profile("m4_silencer",31),"base capacity remains 30, not current total");
		// Captured live token 83: M203 + EOTech selected the correct support pose,
		// but its m4m203_eotech native name previously failed physical admission.
		const m4::attachment* captured[]{&m4::attachments[1]};
		const auto launcher_scene=fixture(captured,"viewhands_us_army").resolve();
		check(launcher_scene.value==&m4::grenadier &&
			native_reload_profile("m4m203_eotech",30,launcher_scene.value->reload)==&m4::physical,
			"captured M203 rifle name and grenadier scene agree on physical reload authority");
		check(!native_reload_profile("m4m203_eotech",1,&m4::physical) &&
			!native_reload_profile("m4m203_eotech",31,&m4::physical) &&
			!native_reload_profile("m203_m4_eotech",1,&m4::physical) &&
			!native_reload_profile("m203_m4_eotech",30,&m4::physical) &&
			!native_reload_profile("m4m203_eotech",30,&ak47::physical) &&
			!m4::native_family(std::string(10000,'a')),
			"M203 alias preserves capacity, separate feed, scene and bounded name guards");
		check(m4::foregrip.wrists[1].position==m4::grenadier.wrists[1].position &&
			m4::foregrip.wrists[1].rotation==m4::grenadier.wrists[1].rotation &&
			m4::foregrip.wrists[0].rotation!=m4::grenadier.wrists[0].rotation,"M203 changes support only");
		for (size_t i=0;i<m4::idle_fingers.size();++i) if (m4::idle_fingers[i].name.find("_ri_")!=std::string_view::npos)
			check(m4::grenadier.fingers[i].rotation==m4::idle_fingers[i].rotation,"M203 rear fingers retained exactly");
		check(physical_reload::valid(m4::physical.interaction) && m4::physical.interaction.locked_travel==0 &&
			!physical_reload::native_action_recoil(m4::physical.interaction),"M4 charging handle is nonreciprocating");
		check(m4::magazine_fingers.size()==15 && m4::handle_fingers.size()==18,"M4 handle includes complete finger and palm chains");
		check(free_hand_rotation(m4::foregrip,0)==free_hand_rotation(m4::grenadier,0),
			"M203 support contact does not rotate shared waist magazine or charging-handle input");
		check(choose_part_grip(m4::handle_grips,m4::handle_grips[0].wrist,{},m4::handle_grab_low,m4::handle_grab_high,39.37007874f).distance_meters<.001f,
			"authored left handle contact lands inside actual rear handle region");
		std::cout<<"M4 assembly combinations checked: "<<combinations<<'\n';
		return failed;
	}
}
