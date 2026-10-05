#pragma once
#include "component/vr/gameplay/weapons/m93r/profile.hpp"
#include "component/vr/gameplay/weapons/tmp/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/rigid_part_visibility.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"
#include "component/vr/gameplay/hand_pose_library.hpp"
#include <iostream>
#include <limits>

namespace foregrip_pistol_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		int failed{}; const auto check=[&](bool ok,const char* why) { if (!ok) { ++failed; std::cerr<<"FAIL: "<<why<<'\n'; } };
		constexpr std::array<std::string_view,14> m93_names{"j_gun","j_bolt","j_handle1","j_rear_press","tag_brass","tag_clip",
			"tag_eotech","tag_flash","tag_rail","tag_red_dot","tag_silencer","tag_stock","j_bullets","j_handle2"};
		constexpr std::array<std::string_view,14> tmp_names{"j_gun","j_bolt","j_reload","j_stock","j_trigger","tag_brass",
			"tag_clip","tag_eotech","tag_flash","tag_red_dot","tag_silencer","j_bullet01","j_bullet02","j_bullet03"};
		for (const auto* grip:{&m93r::base,&tmp::base}) for (auto glove:{"viewhands_us_army","viewhands_arctic"})
		{
			const bool m93=grip==&m93r::base; const auto& p=*grip->reload;
			check(native_reload_profile(p.native_name,p.ammunition.magazine_capacity)==&p,"foregrip pistol exact identity binds");
			check(!native_reload_profile(p.native_name,p.ammunition.magazine_capacity+1),"plus-one total is not base capacity");
			check(grip->aiming==(m93 ? aim_rule::rear_hand : aim_rule::two_hand) && grip->authored_rear==1,
				"M93R support keeps rear-hand aim; TMP uses the front/rear baseline");
			if (!m93) check(native_reload_profile("tmp",32)==&p && !native_reload_profile("tmp",15),
				"TMP physical reload admits the observed H2 32-round magazine");
			const auto span=sub(grip->wrists[0].position,grip->wrists[1].position);
			check(length(span)>6 && span[0]>5,"authored support is on the foregrip, not rear pistol grip");
			for (const auto direction:{vec{0,1,0},vec{0,0,1},vec{-1,0,0}})
			{
				const auto target=scale(direction,length(span));
				const auto q=aimed_rotation(grip->aiming,{0,0,0,1},{},target,span);
				check(m93 ? q==quat{0,0,0,1} : length(sub(rotate(q,span),target))<.001f,
					"moving M93R support never steers; TMP two-hand aiming remains stable in extreme directions");
			}
			check(p.rigid_magazine_source==grip->receiver && !p.magazine_model &&
				grip->viewmodel.visibility==part_visibility::rigid_groups,"new adapters reuse exact magazine subset service");
			rig r{}; r.count=82; r.gun=68; r.parent.fill(-1); r.parent[68]=13;
			std::array<bone_definition,82> bones{}; const auto& names=m93 ? m93_names : tmp_names;
			const int magazine=m93 ? 73 : 74;
			for (int i=68;i<82;++i)
			{
				bones[i].name=names[i-68]; bones[i].bind.rotation={0,0,0,1}; r.weapon_bones[i]=true;
				if (i>68) r.parent[i]=68;
			}
			if (m93) { r.parent[80]=magazine; r.parent[81]=70; }
			else for (int i=79;i<82;++i) r.parent[i]=magazine;
			for (int i=68;i<82;++i)
			{
				anchor local{{},{0,0,0,1}};
				for (const auto& rest:grip->equip_rest) if (rest.name==bones[i].name) local=rest.local;
				const auto parent=r.parent[i];
				const auto bind=compose_reload({bones[parent].bind.position,bones[parent].bind.rotation},local);
				bones[i].bind.position=bind.position; bones[i].bind.rotation=bind.rotation; bones[i].parent=parent;
			}
			std::array<model_definition,2> models{{{glove,0,68},{grip->receiver,68,14}}};
			const auto parts=physical_reload::bind_parts(r,bones,p);
			check(parts.valid && parts.magazine==magazine && select_profile(models,r,bones).value==grip,
				"exported 14-bone receivers bind independently of glove label");
			check(m93 ? parts.slide==69 && parts.bolt==-1 : parts.slide==70 && parts.bolt==69,
				"TMP separates handle from internal bolt; M93R uses one slide");
			part_mask hidden=parts.bullet_mask; hidden[magazine/32]|=0x80000000u>>(magazine%32);
			if (m93)
			{
				const std::array<rigid_group_range,2> groups{{{5*64,450,0,564},{12*64,445,564,618}}};
				check(plan_rigid_visibility(groups,68,895,1182,parts.bullet_mask).hidden_groups==2 &&
					plan_rigid_visibility(groups,68,895,1182,hidden).hidden_groups==3,"M93R shared magazine surface hides round/body independently");
				// Equip-reset must preserve the child's parent-local hinge offset,
				// not add the gun-local foregrip location twice.
				const auto child=compose_reload(m93r::equip_rest[1].local,m93r::equip_rest[2].local);
				check(std::abs(child.position[0]*2.54f-13.442047f)<.001f &&
					std::abs(child.position[2]*2.54f-1.150054f)<.001f,"M93R nested foregrip stays at the unfolded exported position");
			}
			else
			{
				const std::array<rigid_group_range,4> groups{{{6*64,882,0,974},{11*64,207,974,234},
					{12*64,207,1208,234},{13*64,207,1442,234}}};
				check(plan_rigid_visibility(groups,68,1503,1676,parts.bullet_mask).hidden_groups==14 &&
					plan_rigid_visibility(groups,68,1503,1676,hidden).hidden_groups==15,"TMP all three round roots belong to magazine visibility");
				const std::array<rigid_group_range,2> action{{{64,716,0,654},{2*64,1193,654,1686}}};
				check(plan_rigid_visibility(action,68,1909,2340,hidden).hidden_groups==0,"magazine masks cannot remove TMP bolt/handle surface");
				auto wrong=p; auto follower=*p.bolt; wrong.bolt=&follower; follower.bone=p.slide_bone;
				check(!physical_reload::bind_parts(r,bones,wrong).valid,"handle cannot alias the follower bolt");
				follower.bone=p.bullets_bone;
				check(!physical_reload::bind_parts(r,bones,wrong).valid,"round cannot become follower bolt");
				follower=*p.bolt; wrong.interaction.motion=physical_reload::action_motion::reciprocating_slide;
				check(!physical_reload::bind_parts(r,bones,wrong).valid,"bolt linkage is explicit charging-handle data only");
				auto invalid=r; invalid.parent[69]=magazine;
				check(!physical_reload::bind_parts(invalid,bones,p).valid,"unreviewed internal bolt parent rejects TMP");
				auto missing=bones; missing[69].name="missing_bolt";
				check(!physical_reload::bind_parts(r,missing,p).valid,"missing follower rejects complete physical admission");
				missing=bones; missing[81].name="missing_round";
				check(!physical_reload::bind_parts(r,missing,p).valid,"missing third TMP round is not silently ignored");
			}
			models[0].name="unreviewed_weapon_attachment"; r.weapon_bones[0]=true;
			check(!select_profile(models,r,bones).value,"unknown weapon-owned attachments cannot inherit base support grip");
			const auto well_z=p.well.position[2]*2.54f;
			check(m93 ? well_z>-5.3f && well_z<-4.8f : well_z>-7.4f && well_z<-6.8f,
				"both wells remain at the grip mouth, above the extended baseplate");
			check(p.sound_key && !p.sound_key(mechanics::effect::shot) && !p.sound_key(mechanics::effect::action_rear),
				"native burst/automatic sounds and compound chamber clips are not doubled");
		}
		for (auto name:{"beretta393_akimbo","beretta393_silencer","m93r","mp9","tmp_akimbo","tmp_silencer","tmp_xmags"})
			check(!native_reload_profile(name,20) && !native_reload_profile(name,15),"unreviewed native variants remain outside physical authority");
		check(m93r::suppress_equip("h2_wpn_pst_beretta393_first_time_pullout") && tmp::suppress_equip("h2_wpn_pst_mp9_pullout_first"),
			"native foregrip flip and automatic equip rack use authored idle presentation");
		for (auto suffix:{"fire","fire_last","reload","reload_empty","akimbo_l_pullout","inspect"})
		{
			check(!m93r::suppress_equip(std::string("h2_wpn_pst_beretta393_")+suffix),"M93R equip policy does not classify burst/reload/dual actions");
			check(!tmp::suppress_equip(std::string("h2_wpn_pst_mp9_")+suffix),"TMP equip policy does not classify fire/reload/dual actions");
		}
		const auto& bolt=tmp::internal_bolt;
		check(valid_bolt(bolt,tmp::reload_interaction.slide_stroke),"TMP exported linkage validates");
		check(bolt_travel(bolt,.008f,false)==0 && bolt_travel(bolt,0,true)==bolt.locked_m &&
			std::abs(bolt_travel(bolt,.08729389f,false)-.04364531f)<1e-7f,
			"handle take-up, half-distance rear stop and independent lock are preserved");
		float previous{};
		check(std::abs(displayed_internal_bolt(tmp::physical,0,0,false,.02f)-.04865016f)<1e-7f &&
			displayed_internal_bolt(tmp::physical,0,0,false,.002f)>.004f,
			"TMP firing uses its own exported 48.65 mm travel without manual handle take-up");
		check(displayed_internal_bolt(tmp::physical,0,0,false,.08f)==0 &&
			displayed_internal_bolt(tmp::physical,.088f,0,false,-1)==bolt.locked_m &&
			displayed_internal_bolt(tmp::physical,0,0,true,1)==bolt.locked_m,
			"TMP fire returns forward; manual rear stop and empty lock keep their existing stroke");
		for(float age:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
			check(action_shot_fraction(age)==0 && displayed_internal_bolt(tmp::physical,0,0,false,age)==0,
				"invalid shot ages cannot produce a TMP fire displacement");
		for (int i=0;i<=1000;++i)
		{
			const float value=bolt_travel(bolt,i*.0001f,false);
			check(std::isfinite(value) && value>=previous && value<=bolt.locked_m,"linkage is bounded and monotonic including overpull"); previous=value;
		}
		check(bolt_travel(bolt,-1,false)==0 && bolt_travel(bolt,std::numeric_limits<float>::quiet_NaN(),true)==0,
			"invalid or negative visual input cannot create unbounded bolt motion");
		auto invalid=bolt; auto curve=tmp::bolt_curve; invalid.travel=curve;
		curve[2].handle_m=curve[1].handle_m;
		check(!valid_bolt(invalid,.088f) && bolt_travel(invalid,.05f,false)==0,"duplicate curve knots reject before division");
		curve=tmp::bolt_curve; curve[2].bolt_m=-1;
		check(!valid_bolt(invalid,.088f),"negative linked travel rejected");
		invalid=bolt; invalid.rest.rotation={}; check(!valid_bolt(invalid,.088f),"invalid linked rest pose rejected");
		invalid=bolt; invalid.locked_m=1; check(!valid_bolt(invalid,.088f),"lock cannot exceed the reviewed follower stroke");
		for(float stroke:{-1.f,2.f,std::numeric_limits<float>::quiet_NaN()})
		{invalid=bolt;invalid.shot_stroke_m=stroke;check(!valid_bolt(invalid,.088f),"invalid authored shot stroke rejects the profile");}
		return failed;
	}
}
