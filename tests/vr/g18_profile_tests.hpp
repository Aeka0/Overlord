#pragma once
#include "component/vr/gameplay/weapons/g18/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/rigid_part_visibility.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"
#include <iostream>

namespace g18_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		int failed{}; const auto check=[&](bool ok,const char* why) { if (!ok) { ++failed; std::cerr<<"FAIL: "<<why<<'\n'; } };
		check(native_reload_profile("glock",32)==&g18::physical,"G18 binds observed H2 native glock / 32");
		for (auto name:{"g18","glock_akimbo","glock_silencer","glock_eotech","glock_xmags","h2_wpn_pst_glock_idle"})
			check(!native_reload_profile(name,32),"G18 unreviewed identities and animation names cannot write ammunition");
		check(!native_reload_profile("glock",33) && !native_reload_profile("glock",34),"G18 total loaded/extended capacity is not base capacity");
		check(g18::suppress_equip("h2_wpn_pst_glock_first_time_pullout") && g18::suppress_equip("h2_wpn_pst_glock_putaway"),"G18 own equip family classified");
		for (auto name:{"h2_wpn_pst_glock_fire","h2_wpn_pst_glock_last_fire","h2_wpn_pst_glock_reload","h2_wpn_pst_glock_akimbo_r_pullout","h2_wpn_pst_m9_pullout"})
			check(!g18::suppress_equip(name),"G18 equip policy preserves native fire/reload and foreign/dual clips");
		check(g18::base.viewmodel.visibility==part_visibility::rigid_groups && g18::physical.receiver_parented_bullets &&
			g18::physical.rigid_magazine_source==g18::base.receiver && !g18::physical.magazine_model,
			"G18 uses explicit sibling round and exact receiver subset, not differently shaped world clip");
		constexpr std::array<std::string_view,10> names{"j_gun","j_bolt","j_bullet","tag_brass","tag_clip",
			"tag_eotech","tag_flash","tag_rail","tag_red_dot","tag_silencer"};
		for (auto glove:{"viewhands_us_army","viewhands_arctic"})
		{
			rig r{}; r.count=78; r.gun=68; r.parent.fill(-1); r.parent[68]=13;
			std::array<bone_definition,78> bones{};
			for (int n=68;n<78;++n)
			{
				bones[n].name=names[n-68]; bones[n].bind.rotation={0,0,0,1}; r.weapon_bones[n]=true;
				if (n>68) r.parent[n]=68;
				bones[n].parent=r.parent[n];
				for (const auto& p:g18::equip_rest) if (p.name==bones[n].name)
				{ bones[n].bind.position=p.local.position; bones[n].bind.rotation=p.local.rotation; }
			}
			std::array<model_definition,2> models{{{glove,0,68},{g18::base.receiver,68,10}}};
			const auto parts=physical_reload::bind_parts(r,bones,g18::physical);
			check(select_profile(models,r,bones).value==&g18::base && parts.valid && parts.magazine==72 && parts.bullets==70,
				"exported G18 ten-bone topology binds with either glove label");
			const auto round=compose_reload(g18::physical.magazine_rest,parts.bullets_in_magazine);
			check(length(sub(round.position,g18::equip_rest[1].local.position))<.00001f,"sibling-round bind offset reconstructs the original seated round");
			std::array<bone,78> pose{};
			for (size_t n=0;n<pose.size();++n) pose[n]=bones[n].bind;
			const auto before=pose;
			auto moved=g18::physical.magazine_rest; moved.position=add(moved.position,{-2,1,-4}); moved.rotation=normalize(quat{.2f,.3f,-.1f,.9f});
			vr::gameplay::hands::pose_math::move_part(r,parts.magazine,moved,pose);
			vr::gameplay::hands::pose_math::move_part(r,parts.bullets,compose_reload(moved,parts.bullets_in_magazine),pose);
			const auto relative=compose_reload(inverse_reload({pose[72].position,pose[72].rotation}),{pose[70].position,pose[70].rotation});
			check(length(sub(relative.position,parts.bullets_in_magazine.position))<.00001f,"seating/tilting moves independent round with magazine");
			for (int n=0;n<r.count;++n) if (n!=70 && n!=72)
				check(pose[n].position==before[n].position && pose[n].rotation==before[n].rotation,"round ownership cannot move frame, slide or hands");
			// Exported material contains frame + round + magazine as separate
			// rigid groups; their draw order may differ at runtime.
			const std::array<rigid_group_range,3> body{{{0,2826,0,2825},{2*64,103,2825,112},{4*64,486,2937,455}}};
			const auto empty=plan_rigid_visibility(body,68,3415,3392,parts.bullet_mask);
			check(empty.valid && empty.hidden_groups==2,"empty G18 magazine hides the sibling round without removing frame");
			auto mask=parts.bullet_mask; mask[72/32]|=0x80000000u>>(72%32);
			const auto absent=plan_rigid_visibility(body,68,3415,3392,mask);
			check(absent.valid && absent.hidden_groups==6,"detached G18 magazine retains frame geometry");
			auto wrong=g18::physical; wrong.receiver_parented_bullets=false;
			check(!physical_reload::bind_parts(r,bones,wrong).valid,"sibling layout requires explicit per-profile permission");
			wrong=g18::physical; wrong.bullets_bone=wrong.magazine_bone;
			check(!physical_reload::bind_parts(r,bones,wrong).valid,"round role cannot alias the entire magazine");
			auto invalid=bones; invalid[70].bind.rotation={};
			check(!physical_reload::bind_parts(r,invalid,g18::physical).valid,"invalid sibling bind cannot invent an offset");
			invalid=bones; invalid[70].name="missing_round";
			check(!physical_reload::bind_parts(r,invalid,g18::physical).valid,"missing G18 round rejects the physical contract");
			r.parent[70]=72;
			check(!physical_reload::bind_parts(r,bones,g18::physical).valid,"unreviewed round reparenting rejects G18");
		}
		check(g18::physical.well.position[2]*2.54f>-6 && g18::physical.well.position[2]*2.54f<-5,
			"G18 mouth stays at grip base instead of extended-magazine floor");
		check(!g18::sound_key(mechanics::effect::shot) &&
			g18::physical.interaction_sound(mechanics::effect::action_rear).kind==sound_reference_kind::notetrack &&
			std::string_view(g18::physical.interaction_sound(mechanics::effect::action_rear).name)=="weap_glock_first_lift_chamber_plr" &&
			std::string_view(g18::sound_key(mechanics::effect::action_close))=="weap_glock_chamber_plr",
			"G18 full rear stroke and closing use verified native keys without replaying automatic shots");
		return failed;
	}
}
