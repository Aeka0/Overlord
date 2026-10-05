#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/spas12/profile.hpp"
#include "component/vr/gameplay/weapons/winchester1200/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/tube_presenter.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"
#include "spas12_data.hpp"
#include "winchester1200_data.hpp"
#include <iostream>

namespace pump_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		int failures{};const auto check=[&](bool good,const char* why){if(!good){++failures;std::cerr<<"FAIL: "<<why<<'\n';}};
		const auto test=[&](const profile& p,const auto& source)
		{
			rig r{};r.count=68+int(source.size());r.gun=68;r.parent.fill(-1);r.parent[68]=13;r.arms[0].wrist=0;r.arms[1].wrist=1;
			std::array<bone_definition,256> bones{};
			for(size_t i=0;i<p.fingers.size();++i){bones[2+i].name=p.fingers[i].name;r.parent[2+i]=0;}
			for(size_t i=0;i<source.size();++i){bones[68+i].name=source[i].name;r.weapon_bones[68+i]=true;if(i)r.parent[68+i]=68+source[i].parent;}
			for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
			const std::array<model_definition,2> models{{{"viewhands_us_army",0,68},{p.receiver,68,int(source.size())}}};
			const auto span=std::span<const bone_definition>(bones.data(),r.count);
			check(select_profile(models,r,span).value==&p && bind_weapon_poses(r,span,p).valid,"captured pump receiver binds whole hand and weapon profile");
			const auto parts=tube::bind_parts(r,span,p.tube);
			check(parts.valid && parts.pump!=parts.bolt && parts.pump!=parts.lifter,"pump, bolt, carrier and shell bind independently");
			if(parts.pump>=0){r.parent[parts.pump]=parts.bolt;check(!tube::bind_parts(r,span,p.tube).valid,"wrong moving-part hierarchy rejected");r.parent[parts.pump]=r.gun;}
			for(auto* joints:{&p.tube->shell_fingers,&p.tube->rack_grips.front().fingers})for(const auto& joint:*joints)
				check(std::count_if(p.fingers.begin(),p.fingers.end(),[&](const auto& f){return f.name==joint.name;})==1,"pump shell and support fingers match one canonical joint each");
			for(float travel:{0.f,p.tube->interaction.rack.slide_stroke*.5f,p.tube->interaction.rack.slide_stroke})for(bool right:{false,true})
			{
				const auto pose=right ? vr::gameplay::hands::pose_mirror::part(p.tube->rack_grips.front(),{1,0,0,0}) : p.tube->rack_grips.front();
				const auto support=right ? vr::gameplay::hands::pose_mirror::wrist(p.wrists[0],{1,0,0,0}) : p.wrists[0];
				check(length(sub(pose.wrist.position,support.position))<.001f,"right pump contact and ordinary support use the same side of the fore-end");
				const auto offset=scale(p.tube->interaction.rack.slide_axis,travel*39.37007874f);auto wrist=pose.wrist;wrist.position=add(wrist.position,offset);
				check(choose_part_grip({&pose,1},wrist,offset,p.tube->rack_low,p.tube->rack_high,39.37007874f).distance_meters<.001f,"pump hand contact follows moving mesh throughout stroke on either side");
			}
			if(p.receiver==spas12::arctic.receiver)
			{
				// Live oilrig arctic-reflex composite: sight root attaches to the
				// receiver's tag_red_dot, with one reticle child. No new pose recipe.
				const int optic=r.count;r.count+=2;
				bones[optic]={"tag_red_dot",68+12,{{0,0,0,1},{},1}};
				bones[optic+1]={"tag_reticle_red_dot",optic,{{0,0,0,1},{},1}};
				r.parent[optic]=68+12;r.parent[optic+1]=optic;
				r.weapon_bones[optic]=r.weapon_bones[optic+1]=true;
				std::array<model_definition,3> assembly{models[0],models[1],{"attach_h2_red_dot_sight_vm_arctic",optic,2}};
				const auto all=std::span<const bone_definition>(bones.data(),r.count);
				check(select_profile(assembly,r,all).value==&spas12::arctic,"captured arctic reflex assembly shares the arctic pump binding");
				r.parent[optic]=68+11;bones[optic].parent=68+11;
				check(!select_profile(assembly,r,all).value,"misattached arctic optic remains rejected");
			}
		};
		test(spas12::base,spas12_data::bones);test(spas12::arctic,spas12_data::bones);test(winchester1200::base,winchester1200_data::bones);
		for(const auto name:spas12::arctic_names)
		{
			check(spas12::arctic_feed.matches_native(name,7) && !spas12::feed.matches_native(name,7),"arctic native variants require the arctic receiver recipe");
			check(!spas12::arctic_feed.matches_native(name,8),"arctic variant capacity mismatch rejected");
		}
		check(!spas12::arctic_feed.matches_native("spas12_arctic_unknown",7),"unreviewed arctic names remain excluded");
		for(const auto& style:ak47::bolt_grips)
		{
			auto plain=style;plain.opposite_pose.reset();const auto before=vr::gameplay::hands::pose_mirror::part(plain,{1,0,0,0});const auto after=vr::gameplay::hands::pose_mirror::part(style,{1,0,0,0});
			check(after.wrist.position[1]<before.wrist.position[1],"AK independently fitted right grasp keeps the palm outside the receiver");
			const auto contact=[](const auto& pose){return vr::gameplay::hands::pose_math::compose(pose.wrist,{pose.contact_in_wrist,{0,0,0,1}}).position;};
			check(length(sub(contact(before),contact(after)))<.0001f && contact(after)[1]<-2.f,"AK opposite-hand fitting preserves real right tab contact");
		}
		return failures;
	}
}
