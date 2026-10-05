#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/aug/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/rigid_part_visibility.hpp"
#include <iostream>
#include <vector>

namespace latched_handle_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		namespace p=physical_reload;
		int failures{};
		const auto check=[&](bool ok,const char* why) { if (!ok) { ++failures; std::cerr<<"FAIL: "<<why<<'\n'; } };
		// Skeletons independently recorded from the native receiver exports.
		constexpr std::string_view mp5_names[]{"j_gun","j_clip_release","j_front_strap_base","j_rear_strap_base","j_reload","j_trigger",
			"tag_acog_2","tag_brass","tag_clip","tag_eotech","tag_flash","tag_foregrip","tag_red_dot","tag_sight_on","tag_silencer",
			"tag_thermal_scope","j_bullet","j_front_strap","j_rear_strap","j_front_strap_mid","j_front_strap_end"};
		constexpr int mp5_parents[]{-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,8,2,3,17,19};
		constexpr std::string_view aug_names[]{"j_gun","j_reload","j_ring","tag_acog_2","tag_brass","tag_clip","tag_eotech","tag_flash",
			"tag_foregrip","tag_heartbeat","tag_red_dot","tag_sight_on","tag_silencer","tag_steyr_rail","tag_steyr_scope","tag_thermal_scope","j_bullets","j_ring_end"};
		constexpr int aug_parents[]{-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,2};
		constexpr std::string_view ump_names[]{"j_gun","j_bolt","j_string","tag_acog_2","tag_brass","tag_clip","tag_eotech","tag_flash",
			"tag_heartbeat","tag_red_dot","tag_sight_on","tag_silencer","tag_thermal_scope","j_bullet"};
		constexpr int ump_parents[]{-1,0,0,0,0,0,0,0,0,0,0,0,0,5};
		struct fixture
		{
			rig r{}; std::array<bone_definition,256> bones{}; std::vector<model_definition> models;
			fixture(const profile& expected,std::span<const std::string_view> names,std::span<const int> parents)
			{
				r.parent.fill(-1); r.gun=68; r.count=68+static_cast<int>(names.size()); r.arms[0].wrist=0; r.arms[1].wrist=1;
				models={{"viewhands_us_army",0,68},{expected.receiver,68,static_cast<int>(names.size())}};
				for (size_t n=0;n<names.size();++n)
				{ bones[68+n].name=names[n]; r.weapon_bones[68+n]=true; r.parent[68+n]=parents[n]<0 ? 13 : 68+parents[n]; }
				for (size_t n=0;n<expected.fingers.size();++n)
				{ bones[2+n].name=expected.fingers[n].name; r.parent[2+n]=bones[2+n].name.find("_le")!=std::string_view::npos ? 0 : 1; }
				for (int n=0;n<r.count;++n) { bones[n].parent=r.parent[n]; bones[n].bind.rotation={0,0,0,1}; }
			}
			void attach(const assembly_attachment& a)
			{
				int parent=-1; for (int n=68;n<models[1].begin+models[1].count;++n) if (bones[n].name==a.contract.receiver_parent) parent=n;
				const int begin=r.count; models.push_back({a.contract.model,begin,a.bones});
				for (int n=0;n<a.bones;++n)
				{ r.weapon_bones[begin+n]=true; r.parent[begin+n]=n ? begin : parent; bones[begin+n].parent=r.parent[begin+n]; bones[begin+n].bind.rotation={0,0,0,1}; }
				bones[begin].name=a.contract.root;
				if (!a.contract.muzzle.empty()) { bones[begin+1].name=a.contract.muzzle; bones[begin+1].bind.position={6,0,0}; }
				r.count+=a.bones;
			}
			profile_match resolve() const { return select_profile(models,r,{bones.data(),size_t(r.count)}); }
			p::part_rig parts(const reload_profile& d) const { return p::bind_parts(r,{bones.data(),size_t(r.count)},d); }
		};
		for (int family=0;family<3;++family)
		{
			const std::span<const profile> assemblies=family==0 ? std::span<const profile>(mp5::assemblies) : family==1 ? std::span<const profile>(aug::assemblies) : std::span<const profile>(ump::assemblies);
			const std::span<const std::string_view> names=family==0 ? std::span<const std::string_view>(mp5_names) : family==1 ? std::span<const std::string_view>(aug_names) : std::span<const std::string_view>(ump_names);
			const std::span<const int> parents=family==0 ? std::span<const int>(mp5_parents) : family==1 ? std::span<const int>(aug_parents) : std::span<const int>(ump_parents);
			for (const auto& expected:assemblies)
			{
				const auto& d=*expected.reload;
				const unsigned aug_grip=expected.receiver=="h2_viewmodel_steyr_base" ? 3u : 0u;
				for (int optic=-1;optic<14;++optic) for (bool silenced:{false,true})
				{
					fixture f(expected,names,parents);
					if (family==1) { f.attach(aug::specific_attachments[aug_grip]); f.attach(aug::specific_attachments[1]); }
					if (optic>=0) f.attach(rifle_attachments::common[optic]); // Includes silencer; duplicate below must reject.
					if (silenced) f.attach(rifle_attachments::common[0]);
					const bool duplicate=optic==0 && silenced;
					check(duplicate ? !f.resolve().value : f.resolve().value==&expected,"manual catch assembly validates receiver skin, accessory topology and duplicate roles");
					check(f.parts(d).valid,"real magazine, bullet child and handle roots bind for every reviewed receiver");
					check(bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},expected).valid,"complete support, manipulation and native part chains bind");
				}
				fixture f(expected,names,parents);
				if (family==1)
				{
					check(!f.resolve().value,"AUG bare receiver does not fabricate a foregrip assembly");
					f.attach(aug::specific_attachments[aug_grip]); f.attach(aug::specific_attachments[2]);
					check(f.resolve().value==&expected,"AUG original scope shares the actual foregrip and reload recipe");
					auto duplicate=f; duplicate.attach(aug::specific_attachments[1]);
					check(!duplicate.resolve().value,"AUG original scope and rail cannot coexist");
				}
				check(f.resolve().value==&expected,"live bare SMG or scoped AUG complete assembly matched");
				auto unknown=f; unknown.attach(rifle_attachments::common[0]); unknown.models.back().name="unreviewed_attachment";
				check(!unknown.resolve().value,"unknown weapon geometry cannot inherit an authored hand fit");
				const auto parts=f.parts(d);
				check(parts.partition_root==(family==1?-1:f.r.gun),"MP5 and UMP replace only the receiver rigid group; AUG retains its original geometry");
				for (int bone:{parts.magazine,parts.slide,parts.bullets})
				{ auto bad=f; bad.r.parent[bone]=0; check(!bad.parts(d).valid,"moving parts cannot bind to hands or unrelated parents"); }
				check(d.ammunition.magazine_capacity==(family==2 ? 25 : 30) && d.ammunition.plus_one && d.ammunition.manual_catch &&
					d.ammunition.last_round_lock==(family==2) && d.ammunition.physical_catch_release==(family==2) && !d.ammunition.release_control && d.ammunition.release==mechanics::magazine_release::physical_pull,
					"live capacities, UMP follower/paddle release and separate controller-button policy are explicit");
				check(expected.aiming==aim_rule::two_hand && expected.fingers.size()==36 && d.magazine_fingers.size()==18 &&
					d.slide_grips.size()==2 && d.slide_grips[0].fingers.size()==18 && d.slide_grips[1].fingers.size()==18,"both palm facings retain complete native finger/palm chains");
				check(p::valid(d.interaction) && d.interaction.manual_magazine->spare_strike && valid_catch(*d.handle_catch),"MP5, UMP and AUG support magazine latch strikes independently of their manual handle catch");
				check(choose_part_grip(d.slide_grips,d.slide_grips[0].wrist,{},d.slide_grab_low,d.slide_grab_high,39.37007874f).distance_meters<.001f,
					"native closed grasp contact lies on the actual handle tab");
				for (auto gun:{anchor{{},{0,0,0,1}},anchor{{200,-50,12},normalize({.2f,-.4f,.1f,.8f})}})
				{
					const auto seated=compose_reload(gun,d.magazine_rest);
					check(magazine_alignment(d,gun,seated)>.999f,"seated magazine orientation follows an arbitrarily rotated gun");
					const auto tip=magazine_tip_in_well(d,gun,p::translate_local(seated,magazine_exit_translation(d,39.37007874f)),39.37007874f);
					check(tip[2]<=-.0099f && std::abs(tip[0])<.001f && std::abs(tip[1])<.001f,"physical magazine clears its own insertion rail without lateral drift");
				}
				const auto raised=handle_pose(d.slide_rest,d.handle_catch,d.interaction.slide_axis,d.interaction.slide_stroke*39.37007874f,1);
				const auto hinge=compose_reload(d.slide_rest,{d.handle_catch->hinge,{0,0,0,1}});
				const auto raised_hinge=compose_reload(raised,{d.handle_catch->hinge,{0,0,0,1}});
				check(length(sub(raised_hinge.position,add(hinge.position,scale(d.interaction.slide_axis,d.interaction.slide_stroke*39.37007874f))))<1e-5f,
					"raising the handle preserves its guide hinge on the rearward rail");
				const auto base=compose_reload(d.slide_rest,{d.handle_catch->contact,{0,0,0,1}});
				const auto moved=carry_with_handle(d.slide_rest,raised,base);
				const auto direct=compose_reload(raised,{d.handle_catch->contact,{0,0,0,1}});
				check(length(sub(moved.position,direct.position))<1e-5f && moved.position[2]>base.position[2],"offset tab rotates upward around its own pivot while moving rearward");
				check(std::abs(length(sub(moved.position,raised.position))-length(sub(base.position,d.slide_rest.position)))<1e-5f,"catch rotation preserves handle offset radius");
				const auto raised_wrist=carry_with_handle(d.slide_rest,raised,d.slide_grips[0].wrist);
				check(length(sub(compose_reload(raised_wrist,{d.slide_grips[0].contact_in_wrist,{0,0,0,1}}).position,moved.position))<.001f,"raised grasp retains the same contact through the pivot arc");
				auto bad=d; bad.handle_catch=nullptr; check(!f.parts(bad).valid,"mechanics cannot opt into a catch without its visual contact recipe");
				bad=d; bad.interaction.manual_catch=nullptr; check(!f.parts(bad).valid,"visual catch cannot bypass gesture admission");
				const auto native=family==0 ? "mp5_reflex" : family==1 ? "aug_scope_arctic" : "ump45_digital_eotech";
				check(native_reload_profile(native,d.ammunition.magazine_capacity,&d)==&d && !native_reload_profile(native,d.ammunition.magazine_capacity+1,&d),"native mode/capacity discovery preserves the exact selected skin");
				check(!d.matches_native("m203_aug",1) && !d.native_family(std::string(10000,'a')),"alternate feeds and oversized native names fail authority selection");
				const auto take=d.interaction_sound(mechanics::effect::magazine_take);
				const auto out=d.interaction_sound(mechanics::effect::magazine_out);
				check(take.name && out.name && std::string_view(take.name)==out.name,
					"physical magazine removal resolves the verified native clip-out audio");
				auto sound_profile=d;
				sound_profile.sound_override=[](mechanics::effect event) noexcept -> sound_reference {
					return {event==mechanics::effect::magazine_take ? "explicit_take" : nullptr};
				};
				check(std::string_view(sound_profile.interaction_sound(mechanics::effect::magazine_take).name)=="explicit_take",
					"specific take audio takes priority over the removal fallback");
				part_mask mask=parts.bullet_mask; mask[parts.magazine/32]|=0x80000000u>>(parts.magazine%32);
				if (family==2)
				{
					const std::array<rigid_group_range,3> groups{{{5*64,1673,0,2267},{13*64,266,2267,360},{64,271,2627,361}}};
					const auto plan=plan_rigid_visibility(groups,68,2210,2988,mask);
					check(plan.valid && plan.hidden_groups==3,"UMP magazine removal preserves the handle sharing its material surface");
				}
				if (family==0)
				{
					part_mask strap{}; strap[0]=22528; // Native MP5 skinned strap part bits.
					check(!surface_intersects(strap,68,mask),"MP5 magazine mask excludes its unrelated native skinned strap");
				}
			}
		}
		check(aug::magazine_well.position[0]<aug::wrists[1].position[0],"AUG bullpup magazine well stays behind the primary hand");
		return failures;
	}
}
