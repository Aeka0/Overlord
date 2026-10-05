#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/de50/profile.hpp"
#include "component/vr/gameplay/weapons/dragunov/profile.hpp"
#include "component/vr/gameplay/weapons/m1014/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/weapons/m82/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "dragunov_data.hpp"
#include <iostream>
#include <vector>

namespace dragunov_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		int failures{};const auto check=[&](bool good,const char* why){if(!good){++failures;std::cerr<<"FAIL: "<<why<<'\n';}};
		const auto test=[&](const profile& p,const auto& source,bool scope)
		{
			rig r{};r.count=68+int(source.size());r.gun=68;r.parent.fill(-1);r.parent[68]=13;r.arms[0].wrist=0;r.arms[1].wrist=1;
			std::array<bone_definition,256> bones{};
			for(size_t i=0;i<p.fingers.size();++i){bones[2+i].name=p.fingers[i].name;r.parent[2+i]=0;}
			for(size_t i=0;i<source.size();++i){bones[68+i].name=source[i].name;r.weapon_bones[68+i]=true;if(i)r.parent[68+i]=68+source[i].parent;}
			std::vector<model_definition> models{{"viewhands_us_army",0,68},{p.receiver,68,int(source.size())}};
			if(scope)
			{
				int parent=-1;for(int i=68;i<r.count;++i)if(bones[i].name=="tag_sight_on")parent=i;
				const int start=r.count;models.push_back({p.receiver==dragunov::arctic.receiver ? "attach_h2_dragunov_scope_vm_arctic" : "attach_h2_dragunov_scope_vm",start,4});
				for(const auto& b:dragunov_data::scope){bones[r.count].name=b.name;r.parent[r.count]=b.parent<0 ? parent : start+b.parent;r.weapon_bones[r.count++]=true;}
			}
			for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
			const auto span=std::span<const bone_definition>(bones.data(),r.count);
			check(select_profile(models,r,span).value==&p && bind_weapon_poses(r,span,p).valid,"captured Dragunov/gold receiver binds its complete profile");
			check(physical_reload::bind_parts(r,span,*p.reload).valid,"captured magazine, slide and ammunition hierarchy binds without remapping parents");
			for(const auto& style:p.reload->slide_grips)for(bool right:{false,true})for(float stroke:{0.f,p.reload->interaction.slide_stroke})
			{
				const auto pose=right ? vr::gameplay::hands::pose_mirror::part(style,{1,0,0,0}) : style;
				const auto offset=scale(p.reload->interaction.slide_axis,stroke*39.37007874f);auto wrist=pose.wrist;wrist.position=add(wrist.position,offset);
				check(choose_part_grip({&pose,1},wrist,offset,p.reload->slide_grab_low,p.reload->slide_grab_high,39.37007874f).distance_meters<.002f,"operating hands retain source contacts at both ends of bolt travel");
			}
		};
		test(dragunov::base,dragunov_data::receiver,false);test(dragunov::base,dragunov_data::receiver,true);test(de50::gold,dragunov_data::gold,false);
		test(dragunov::arctic,dragunov_data::receiver,true);
		check(native_reload_profile("dragunov_arctic",10)==&dragunov::arctic_physical && !native_reload_profile("dragunov_arctic",11) &&
			!native_reload_profile("dragunov_arctic_unknown",10),"captured arctic native identity is exact and capacity checked");
		check(native_reload_profile("deserteagle_gold",7)==&de50::physical && de50::gold.reload==de50::base.reload,"gold Desert Eagle shares existing mechanics with its own render identity");
		check(!native_reload_profile("deserteagle_gold",8) && !native_reload_profile("deserteagle_gold_unknown",7),"gold admission remains exact and capacity checked");
		check(native_reload_profile("dragunov",10)==&dragunov::physical && !native_reload_profile("dragunov",7),"Dragunov admits its captured native capacity");

		for(const auto* definition:{&ak47::physical,&m14ebr::physical,&dragunov::physical})for(size_t i=0;i<2;++i)
		{
			const auto& left=definition->slide_grips[i];const auto right=vr::gameplay::hands::pose_mirror::part(left,{1,0,0,0});
			const auto contact=[](const auto& p){return vr::gameplay::hands::pose_math::compose(p.wrist,{p.contact_in_wrist,{0,0,0,1}}).position;};
			check(length(sub(contact(left),contact(right)))<.00002f,"independently fitted hands keep the same real handle contact");
			check(left.opposite_pose && right.fingers.data()==left.fingers.data(),"both fitted hands use the selected wrapped finger chain");
			check(left.wrist.position[1]<contact(left)[1] && right.wrist.position[1]<contact(right)[1],"both wrists remain outside the right-side receiver");
		}
		for(const auto* original:{&m1014::rack_pose})for(const auto basis:{quat{1,0,0,0},normalize({.2f,.7f,-.1f,.6f})})
		{
			const auto grips=m1014::rack_grips;
			const auto before=vr::gameplay::hands::pose_mirror::part(*original,basis),after=vr::gameplay::hands::pose_mirror::part(grips[0],basis);
			check(length(sub(before.wrist.position,after.wrist.position))<.00002f && before.wrist.rotation==after.wrist.rotation && before.fingers.data()==after.fingers.data(),"M1014 retains its accepted native right index wrist and fingers");
			const auto pinky=vr::gameplay::hands::pose_mirror::part(grips[1],basis);
			check(std::abs(dot(rotate(after.wrist.rotation,{0,0,1}),rotate(pinky.wrist.rotation,{0,0,1})))<.999f,"new right pinky is a distinct grasp rather than a renamed index pose");
			for(bool right:{false,true})
			{
				std::array<part_grip_pose,2> poses=grips;
				if(right)for(auto& pose:poses)pose=vr::gameplay::hands::pose_mirror::part(pose,basis);
				for(size_t i=0;i<poses.size();++i)
					check(choose_part_grip(poses,poses[i].wrist,{},vec{-100,-100,-100},vec{100,100,100},39.37007874f).pose==i,"both shotgun styles are independently selectable on either hand");
			}
		}

		for(size_t i=0;i<m82::action_grips.size();++i)for(bool right:{false,true})
		{
			const auto pose=right?vr::gameplay::hands::pose_mirror::part(m82::action_grips[i],{1,0,0,0}):m82::action_grips[i];
			check(pose.fingers.size()==18 && pose.fingers.data()==hand_poses::edge_handle::source_styles[i].fingers.data(),
				"M82 both-hand hooks use complete shared finger chains without the legacy native-right override");
		}
		check(m82::action_grips.size()==m82::reload_interaction.slide_pose_count,"M82 simulation and renderer agree on both charging styles");
		for(const auto& style:m82::action_grips)for(bool right:{false,true})for(float travel:{0.f,m82::action_stroke_m})
		{
			const auto pose=right ? vr::gameplay::hands::pose_mirror::part(style,{1,0,0,0}) : style;
			const auto offset=scale(m82::reload_interaction.slide_axis,travel*39.37007874f);auto wrist=pose.wrist;wrist.position=add(wrist.position,offset);
			check(choose_part_grip({&pose,1},wrist,offset,m82::action_grab_low,m82::action_grab_high,39.37007874f).distance_meters<.002f,"M82 shared index and pinky hooks retain real contacts through full travel");
		}

		for(auto* p:m14ebr::skins)
		{
			check(p->slide_grips.size()==2 && p->interaction.slide_pose_count==2,"M14 and M21 expose both charging styles");
			for(size_t i=0;i<2;++i)
			{
				check(p->slide_grips[i].fingers.data()==ak47::bolt_grips[i].fingers.data() && p->slide_grips[i].wrist.rotation==ak47::bolt_grips[i].wrist.rotation,"M14/M21 reuse AK finger chains and style orientation");
				const auto pose=p->slide_grips[i];
				check(choose_part_grip({&pose,1},pose.wrist,{},p->slide_grab_low,p->slide_grab_high,39.37007874f).distance_meters<.001f,"M14 styles fit the M14 tab rather than AK coordinates");
			}
		}
		return failures;
	}
}
