#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include <vector>
#include <iostream>

namespace m16_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		int failed{};const auto check=[&](bool value,const char* why) {if (!value) {++failed;std::cerr<<"FAIL: "<<why<<'\n';}};
		struct fixture
		{
			rig r{};std::array<bone_definition,256> bones{};std::vector<model_definition> models;
			fixture(std::span<const assembly_attachment* const> items,std::string_view glove)
			{
				r.parent.fill(-1);r.gun=68;r.count=96;r.parent[68]=1;r.arms[0].wrist=2;r.arms[1].wrist=3;
				models={{glove,0,68},{m16::bare.receiver,68,28}};
				for (size_t i=0;i<m16::idle_fingers.size();++i)
				{bones[4+i].name=m16::idle_fingers[i].name;r.parent[4+i]=bones[4+i].name.find("_le")!=std::string_view::npos ? 2 : 3;}
				constexpr std::string_view names[]{"j_gun","j_bolt_catch","j_brass_door","j_clip_release_latch","j_front_ring",
					"j_gun_trigger","j_reload","j_safety","tag_acog_2","tag_armor","tag_brass","tag_clip","tag_eotech",
					"tag_flash","tag_foregrip","tag_heartbeat","tag_laser","tag_m203","tag_red_dot","tag_shotgun",
					"tag_sight_on","tag_silencer","tag_thermal_scope","j_bullets","j_front_ring_1","j_sight_ring","j_front_ring_2","j_front_ring_3"};
				for (int i=68;i<96;++i) {bones[i].name=names[i-68];r.weapon_bones[i]=true;if (i>68) r.parent[i]=68;}
				r.parent[91]=79;r.parent[92]=72;r.parent[93]=88;r.parent[94]=92;r.parent[95]=94;
				for (const auto* item:items)
				{
					int parent=-1;for (int i=68;i<96;++i) if (bones[i].name==item->contract.receiver_parent) parent=i;
					const auto begin=r.count;models.push_back({item->contract.model,begin,item->bones});
					for (int n=0;n<item->bones;++n) {r.weapon_bones[begin+n]=true;r.parent[begin+n]=n ? begin : parent;}
					bones[begin].name=item->contract.root;
					if (!item->contract.muzzle.empty()) {bones[begin+1].name=item->contract.muzzle;bones[begin+1].bind.position={6.7f,0,0};}
					r.count+=item->bones;
				}
				for (int i=0;i<r.count;++i) {bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
			}
			profile_match resolve() const {return select_profile(models,r,{bones.data(),size_t(r.count)});}
		};
		unsigned combinations{};
		for (auto glove:{"viewhands_us_army","viewhands_arctic"})
		for (int under=-1;under<2;++under) for (int optic=-1;optic<13;++optic)
		for (bool suppressor:{false,true}) for (bool reverse:{false,true})
		{
			std::vector<const assembly_attachment*> items;
			if (under>=0) items.push_back(&m16::attachments[under]);
			if (suppressor) items.push_back(&m16::attachments[2]);
			if (optic>=0) items.push_back(&m16::attachments[3+optic]);
			if (reverse) std::reverse(items.begin(),items.end());
			fixture f(items,glove);const auto result=f.resolve();
			check(result.value==(under==1 ? &m16::grenadier : &m16::bare),"M16 bare/armor has handguard support and M203 changes only support");
			check((result.muzzle>=0)==suppressor,"M16 suppressor supplies its own muzzle");
			for (int bone=0;bone<256;++bone)
				check(bool(result.hidden[bone/32]&(0x80000000u>>(bone%32)))==(optic>=0 && (bone==88 || bone==93)),
					"M16 optics hide only the native carry handle and sight ring; iron sights retain both");
			check(result.value && result.value->reload==&m16::physical,"M16 variants share one independent physical definition");
			const auto parts=physical_reload::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},m16::physical);
			check(parts.valid && parts.partition_root==parts.slide,"M16 bolt partitions its real charging-handle group while rounds remain on the magazine");
			check(result.value && bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},*result.value).valid,"complete M16 finger and equip data bind across variants");
			++combinations;
		}
		const assembly_attachment* cover[]{&m16::attachments[0]};fixture broken(cover,"viewhands_us_army");
		broken.r.parent[96]=2;check(!broken.resolve().value,"M16 handguard cannot attach to a wrist");
		const assembly_attachment* conflict[]{&m16::attachments[0],&m16::attachments[1]};
		check(!fixture(conflict,"viewhands_us_army").resolve().value,"M16 armor and M203 conflict rejected");
		const assembly_attachment* duplicate[]{cover[0],cover[0]};check(!fixture(duplicate,"viewhands_us_army").resolve().value,"M16 duplicate cover rejected");
		const assembly_attachment* wrong[]{&m4::attachments[0]};check(!fixture(wrong,"viewhands_us_army").resolve().value,"M16 never silently adopts M4 vertical foregrip");
		const assembly_attachment* optic[]{&m16::attachments[3]};
		fixture bad_sight(optic,"viewhands_us_army");bad_sight.r.parent[93]=68;
		check(!bad_sight.resolve().value,"M16 optics cannot hide a sight ring outside the carry handle");
		bad_sight=fixture(optic,"viewhands_us_army");bad_sight.bones[88].name="unknown_sight";
		check(!bad_sight.resolve().value,"M16 missing carry handle contract rejected");
		fixture bare({},"viewhands_us_army");
		bare.r.parent[91]=68;check(!physical_reload::bind_parts(bare.r,{bare.bones.data(),size_t(bare.r.count)},m16::physical).valid,"M16 bullets with incorrect parent rejected");
		for (auto name:{"m16","m16_basic","m16_reflex","m16_acog","m16_grenadier"})
			check(native_reload_profile(name,30)==&m16::physical,"M16 dumped native rifle identity registered");
		for (auto name:{"m160","m16_","m16/invalid","m203_m16","m4","m16a4"})
			check(native_reload_profile(name,30)!=&m16::physical,"unverified native name or alternate feed not M16 rifle");
		check(!native_reload_profile("m16_basic",31,&m16::physical) && !native_reload_profile("m16_basic",30,&m4::physical),"M16 capacity and scene identity stay independent of M4");
		check(m16::bare.wrists[1].position==m16::grenadier.wrists[1].position && m16::bare.wrists[0].rotation!=m16::grenadier.wrists[0].rotation,
			"M16 launcher keeps authored rear grip and changes support contact");
		check(free_hand_rotation(m16::bare,0)==free_hand_rotation(m16::grenadier,0),"M16 support variant preserves manipulation basis");
		check(physical_reload::valid(m16::interaction) && m16::interaction.locked_travel==0 && !physical_reload::native_action_recoil(m16::interaction),"M16 nonreciprocating handle remains independent of bolt lock");
		check(m16::interaction.slide_stroke>.08f && m16::interaction.slide_stroke<.083f,"M16 uses measured 8.1 cm handle travel rather than M4 stroke");
		check(choose_part_grip(m16::handle_grips,m16::handle_grips[0].wrist,{},m16::handle_grab_low,m16::handle_grab_high,39.37007874f).distance_meters<.001f,
			"M16 authored finger contact reaches rear handle geometry");
		check(m16::suppress_equip("h2_wpn_asl_m16_pullout_first") && m16::suppress_equip("h2_wpn_asl_m16_gl_pullout") && !m16::suppress_equip("h2_wpn_asl_m16_reload"),
			"M16 native equip animation suppressed without reclassifying reload");
		std::cout<<"M16 assembly combinations checked: "<<combinations<<'\n';return failed;
	}
}
