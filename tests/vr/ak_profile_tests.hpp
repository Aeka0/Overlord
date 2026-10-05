#pragma once
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include <vector>
#include <iostream>
#include "ak_hand_tests.hpp"
#include "ak_desert_data.hpp"

namespace ak_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		int failed{};
		const auto check=[&](bool v,const char* why) { if (!v) { ++failed; std::cerr<<"FAIL: "<<why<<'\n'; } };
		struct fixture
		{
			rig r{}; std::array<bone_definition,256> bones{}; std::vector<model_definition> models;
			fixture(int skin,std::span<const assembly_attachment* const> items,std::string_view glove="viewhands_us_army")
			{
				r.parent.fill(-1); r.gun=68; r.count=93; r.parent[68]=13;
				models={{glove,0,68},{ak47::skins[skin]->rigid_magazine_source,68,25}};
				constexpr std::string_view names[]{"j_gun","j_back_ring","j_bar","j_bolt2","j_front_ring_base",
					"j_gadget","j_gun_trigger","j_trigger","tag_acog_2","tag_brass","tag_clip","tag_clip_02",
					"tag_cover","tag_eotech","tag_flash","tag_gp25","tag_heartbeat","tag_red_dot","tag_shotgun",
					"tag_silencer","tag_thermal_scope","j_bullet01","j_bullet02","j_bullet03","j_front_ring_end"};
				for (int i=68;i<93;++i) { bones[i].name=names[i-68]; r.weapon_bones[i]=true; if (i>68) r.parent[i]=68; }
				r.arms={arm{8,14,24},arm{9,15,25}}; r.rear_grip_wrist=25; r.weapon_tag=13;
				r.parent[14]=8; r.parent[24]=14; r.parent[15]=9; r.parent[25]=15;
				bones[8].bind.position={-12,4,0}; bones[14].bind.position={-4,4,-6}; bones[24].bind.position={4,4,0};
				bones[9].bind.position={-12,-4,0}; bones[15].bind.position={-4,-4,-6}; bones[25].bind.position={4,-4,0};
				bones[68].bind.position=sub(bones[25].bind.position,ak47::wrists[1].position); bones[13].bind.position=bones[68].bind.position;
				for (size_t i=0;i<ak47::idle_fingers.size();++i) bones[28+i].name=ak47::idle_fingers[i].name;
				for (size_t i=0;i<ak47::idle_fingers.size();++i)
				{
					const auto name=bones[28+i].name; const bool left=name.find("_le")!=std::string_view::npos;
					int parent=left ? 24 : 25; std::string parent_name;
					if (name.ends_with("_1") || name.ends_with("_2")) { parent_name=name; --parent_name.back(); }
					if (name.starts_with("j_pinky_") && name.ends_with("_0")) parent_name=left ? "j_pinkypalm_le" : "j_pinkypalm_ri";
					if (name.starts_with("j_ring_") && name.ends_with("_0")) parent_name=left ? "j_ringpalm_le" : "j_ringpalm_ri";
					for (int j=28;j<int(28+i);++j) if (bones[j].name==parent_name) parent=j;
					r.parent[28+i]=parent; bones[28+i].bind.position=add(bones[parent].bind.position,{left ? 1.f : -1.f,0,0});
				}
				for (int i=89;i<92;++i) r.parent[i]=78;
				r.parent[92]=72;
				for (const auto* item:items)
				{
					int parent=-1; for (int i=68;i<93;++i) if (bones[i].name==item->contract.receiver_parent) parent=i;
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
			const int skin=ak47::receiver_skin("h2_viewmodel_ak47_base_desert");
			check(skin>=0,"live desert AK receiver is registered rather than falling back to native animation");
			if(skin>=0)
			{
				const assembly_attachment* gp25[]{&ak47::attachments[0]};fixture f(skin,gp25);
				const auto apply=[&](const auto& source,int start,int root){for(size_t i=0;i<source.size();++i)
				{f.bones[start+i].name=source[i].name;f.bones[start+i].parent=f.r.parent[start+i]=source[i].parent<0 ? root : start+source[i].parent;}};
				apply(ak_desert_data::receiver,68,13);apply(ak_desert_data::gp25,93,83);
				const auto matched=f.resolve();check(matched.value && matched.value->variant=="gp25" && matched.value->reload==&ak47::desert,"captured desert receiver and GP25 topology select their complete AK recipe");
				check(physical_reload::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},ak47::desert).valid,"captured desert magazine, bullet chain and charging handle bind");
				check(native_reload_profile("ak47_desert_grenadier",30,&ak47::desert)==&ak47::desert && ak47::desert.slide_grips.data()==ak47::bolt_grips.data(),"live desert native name retains desert render identity and the current shared charging poses");
				check(!native_reload_profile("gl_ak47_desert",1,&ak47::desert),"GP25 alternate feed cannot borrow primary AK reload rules");
				f.models[1].name="h2_viewmodel_ak47_base_unreviewed";check(!f.resolve().value,"adding a witnessed camouflage does not admit arbitrary AK-looking receivers");
			}
		}

		{
			// Keep the witnessed asset independent of the registry so an omitted
			// attachment reproduces the live fallback, rather than skipping a test.
			const assembly_attachment cover{{"attach_h2_ak47_cover_vm_desert","tag_cover","tag_cover"},attachment_role::cover,1};
			const assembly_attachment* items[]{&cover};
			fixture f(ak47::receiver_skin("h2_viewmodel_ak47_base_desert"),items,"viewmodel_base_viewhands");
			for(size_t i=0;i<ak_desert_data::receiver.size();++i)
			{
				const auto& source=ak_desert_data::receiver[i];
				f.bones[68+i].name=source.name;
				f.bones[68+i].parent=f.r.parent[68+i]=source.parent<0 ? 13 : 68+source.parent;
			}
			f.bones[93].name=ak_desert_data::cover[0].name;
			f.bones[93].parent=f.r.parent[93]=80;
			const auto matched=f.resolve();
			check(matched.value && matched.value->variant=="bare" && matched.value->reload==&ak47::desert,
				"captured bare desert AK with its cover selects shared AK physical actions");
			check(native_reload_profile("ak47_desert",30,&ak47::desert)==&ak47::desert &&
				physical_reload::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},ak47::desert).valid,
				"bare desert AK retains magazine, latch and charging-handle mechanics");
			f.bones[93].parent=f.r.parent[93]=68;
			check(!f.resolve().value,"desert cover still requires the correct receiver parent");
			f.bones[93].parent=f.r.parent[93]=80;
			f.models.back().name="attach_h2_ak47_cover_vm_unreviewed";
			check(!f.resolve().value,"desert cover admission does not accept unknown cover aliases");
		}

		unsigned combinations{};
		// Select witnessed attachment names, not offsets into the growing catalog.
		constexpr std::string_view covers[]{"attach_h2_ak47_cover_vm","attach_h2_ak47_cover_vm_arctic",
			"attach_h2_ak47_cover_vm_digital","attach_h2_ak47_cover_vm_desert","attach_h2_ak47_cover_vm_woodland"};
		constexpr std::string_view silencers[]{"attach_h2_silencer_02_vm","attach_h2_silencer_03_vm","attach_h2_silencer_01_vm"};
		const auto attachment=[&](std::string_view name)->const assembly_attachment* {
			for(const auto& a:ak47::attachments)if(a.contract.model==name)return &a;
			check(false,"AK captured attachment is registered");return nullptr;
		};
		static_assert(std::size(covers)==ak47::skins.size());
		{
			// Witnessed registered ak47_desert_reflex composite, independent of
			// catalog enumeration so a missing alias really fails this regression.
			const assembly_attachment cover{{"attach_h2_ak47_cover_vm_desert","tag_cover","tag_cover"},attachment_role::cover,1};
			const assembly_attachment sight{{"attach_h2_red_dot_sight_vm_desert","tag_red_dot","tag_red_dot"},attachment_role::optic,2};
			const assembly_attachment* items[]{&cover,&sight};
			fixture live(ak47::receiver_skin("h2_viewmodel_ak47_base_desert"),items,"viewmodel_base_viewhands");
			live.bones[95].name="tag_reticle_red_dot";
			const auto match=live.resolve();
			check(match.value&&match.value->variant=="bare"&&match.value->reload==&ak47::desert,
				"registered desert AK reflex composite retains rifle binding");
			live.r.parent[94]=80;
			check(!live.resolve().value,"desert optic alias still rejects attachment to the cover tag");
		}
		for (int skin=0;skin<int(ak47::skins.size());++skin) for (int under=0;under<3;++under)
		for (bool cover:{false,true}) for (int silencer=-1;silencer<3;++silencer)
		for (int optic=-1;optic<13;++optic) for (bool sensor:{false,true})
		for (bool reverse:{false,true}) for (auto glove:{"viewhands_us_army","viewhands_test_other_glove"})
		{
			std::vector<const assembly_attachment*> items;
			if (under) items.push_back(&ak47::attachments[under-1]);
			if (cover) items.push_back(attachment(covers[skin]));
			if (silencer>=0) items.push_back(attachment(silencers[silencer]));
			if(std::find(items.begin(),items.end(),nullptr)!=items.end())continue;
			if (optic>=0) items.push_back(&rifle_attachments::common[1+optic]);
			if (sensor) items.push_back(&rifle_attachments::common[14]);
			if (reverse) std::reverse(items.begin(),items.end());
			fixture f(skin,items,glove); const auto resolved=f.resolve();
			check(resolved.value==&ak47::assemblies[skin*3+under],"AK scene selects exact skin and support variant");
			check((resolved.muzzle>=0)==(silencer>=0),"AK underbarrel cannot replace rifle firing origin");
			const auto parts=physical_reload::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},*ak47::skins[skin]);
			check(parts.valid && parts.magazine==78 && parts.slide==71,"AK magazine and right bolt bind independently of accessories");
			for (int b=68;b<f.r.count;++b)
				check(bool(parts.bullet_mask[b/32] & (0x80000000u>>(b%32)))==(b>=89 && b<92),"AK empty-magazine mask contains exactly three round subtrees");
			++combinations;
		}
		for (int skin=0;skin<int(ak47::skins.size());++skin)
		{
			fixture bare(skin,{});
			check(!select_profile(bare.models,bare.r).value,"AK needs topology, not only model names");
			for (int b=89;b<92;++b)
			{
				auto bad=bare; bad.r.parent[b]=68;
				check(!physical_reload::bind_parts(bad.r,{bad.bones.data(),size_t(bad.r.count)},*ak47::skins[skin]).valid,
					"AK round outside magazine rejects physical binding");
				bad=bare; bad.bones[b].name="missing_round";
				check(!physical_reload::bind_parts(bad.r,{bad.bones.data(),size_t(bad.r.count)},*ak47::skins[skin]).valid,
					"AK missing round cannot produce partial visibility");
			}
			for (auto name:{"ak47","ak47_gp25","ak47_shotgun_silencer","ak47_thermal"})
				check(native_reload_profile(name,30,ak47::skins[skin])==ak47::skins[skin],"scene skin survives native AK family binding");
			check(!native_reload_profile("gp25_ak47",1,ak47::skins[skin]) && !native_reload_profile("shotgun_ak47",4,ak47::skins[skin]) &&
				!native_reload_profile("ak47",31,ak47::skins[skin]),"underbarrel feeds and changed capacities cannot enter AK reload");
		}
		for (auto name:{"ak47_","ak47x","ak47/invalid","AK47","ak47_bad-name","m4","gp25_ak47"})
			check(!ak47::native_family(name),"AK native name boundary validation");
		const assembly_attachment* both[]{&ak47::attachments[0],&ak47::attachments[1]};
		check(!fixture(0,both).resolve().value,"AK GP25 and shotgun cannot coexist");
		const assembly_attachment* duplicate[]{both[0],both[0]};
		check(!fixture(0,duplicate).resolve().value,"AK duplicate underbarrel rejected");
		fixture bad(0,{both,1}); bad.r.parent[93]=0;
		check(!bad.resolve().value,"AK underbarrel cannot attach to a hand");
		bad.r.parent[93]=83; bad.models.back().name="attach_h2_unknown_vm";
		check(!bad.resolve().value,"AK unreviewed attachment rejected");
		const assembly_attachment* laser[]{&rifle_attachments::common[15]};
		check(!fixture(0,laser).resolve().value,"shared laser contract cannot invent missing AK receiver tag");
		for (const auto& profile:ak47::assemblies)
		{
			fixture hand_fixture(ak47::receiver_skin(profile.receiver),{});
			ak_hand_tests::run(hand_fixture.r,{hand_fixture.bones.data(),size_t(hand_fixture.r.count)},profile,check);
			check(profile.wrists[1].position==ak47::wrists[1].position && profile.wrists[1].rotation==ak47::wrists[1].rotation &&
				free_hand_rotation(profile,0)==free_hand_rotation(ak47::assemblies[0],0),"AK support variation preserves rear grip and free operation frame");
			for (size_t i=0;i<ak47::idle_fingers.size();++i) if (ak47::idle_fingers[i].name.find("_ri_")!=std::string_view::npos)
				check(profile.fingers[i].rotation==ak47::idle_fingers[i].rotation,"AK attachment cannot rotate rear fingers");
		}
		check(ak47::assemblies[0].wrists[0].position!=ak47::assemblies[1].wrists[0].position &&
			ak47::assemblies[1].wrists[0].position!=ak47::assemblies[2].wrists[0].position,"all three AK support anchors differ");
		for (size_t i=0;i<ak47::bolt_grips.size();++i)
		{
			const auto& grip=ak47::bolt_grips[i];
			const auto chosen=choose_part_grip(ak47::bolt_grips,grip.wrist,{},ak47::bolt_grab_low,ak47::bolt_grab_high,39.37007874f);
			check(chosen.pose==i && chosen.distance_meters<.001f && grip.wrist.position[1]<0,
				"AK selects both left-hand edges on the actual right-side charging tab");
			const auto point=compose_reload(grip.wrist,{grip.contact_in_wrist,{0,0,0,1}}).position;
			check(point[0]*2.54f>21.8f && point[0]*2.54f<23 && point[1]*2.54f< -3,
				"AK grasp contact is on tab geometry, not the old region ten cm behind it");
			check(grip.fingers.size()==18,"both AK grips include finger, palm and webbing joints");
		}
		for (auto prefix:{"h2_wpn_asl_ak47_tac_","h2_wpn_asl_ak47_gl_","h2_wpn_asl_ak47_shotgun_"})
		{
			for (auto action:{"pullout_first","pullout","pullout_quick","putaway"})
				check(ak47::suppress_equip(std::string(prefix)+action),"all primary AK variants suppress native right-hand equip cocking");
			for (auto action:{"fire","reload","reload_empty","grenade_pullout","shotty_pullout","inspect"})
				check(!ak47::suppress_equip(std::string(prefix)+action),"AK equip policy preserves native shots and excludes alternate-feed actions");
		}
		const auto& p=ak47::physical;
		const anchor local_wrist=compose_reload(p.magazine_rest,inverse_reload(p.magazine_in_wrist));
		const auto local=magazine_contacts(p,{},local_wrist,p.magazine_rest,39.37007874f);
		check(local.valid && local.grip_distance<.001f,"authored magazine grip is inside the actual body box");
		for (quat q:{quat{0,0,0,1},normalize(quat{.3f,-.7f,.2f,.4f})})
		{
			const anchor gun{{50,-30,20},q};
			const auto transformed=magazine_contacts(p,gun,compose_reload(gun,local_wrist),compose_reload(gun,p.magazine_rest),39.37007874f);
			check(transformed.valid && std::abs(transformed.grip_distance-local.grip_distance)<.0001f,"magazine grasp invariant under world rotation/translation");
			for (size_t n=0;n<local.strike->region_count;++n)
				check(std::abs(physical_reload::closest_box(*transformed.strike,n).distance-physical_reload::closest_box(*local.strike,n).distance)<.0001f,"whole-magazine contact is invariant under a common world transform");
		}
		std::cout<<"AK assembly combinations checked: "<<combinations<<'\n';
		return failed;
	}
}
