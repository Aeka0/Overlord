#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/m1014/profile.hpp"
#include "component/vr/gameplay/weapons/spas12/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/tube_presenter.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "m1014_data.hpp"
#include <vector>
#include <iostream>

namespace m1014_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		int errors{},count{};const auto check=[&](bool ok,const char* why){if(!ok){++errors;std::cerr<<"FAIL: "<<why<<'\n';}};
		for(const auto& profile:m1014::assemblies)for(int optic:{-1,5,6,7,8,9,10})for(bool silenced:{false,true})for(bool reverse:{false,true})
		{
			rig r{};r.count=80;r.gun=68;r.parent.fill(-1);r.parent[68]=13;r.arms[0].wrist=0;r.arms[1].wrist=1;
			std::array<bone_definition,256> bones{};std::vector<model_definition> models{{"viewhands_us_army",0,68},{profile.receiver,68,12}};
			for(size_t i=0;i<profile.fingers.size();++i){bones[2+i].name=profile.fingers[i].name;r.parent[2+i]=0;}
			const auto& source=profile.tube==&m1014::feed ? m1014_data::base : m1014_data::arctic;
			for(int i=0;i<12;++i){bones[68+i].name=source[i].name;r.weapon_bones[68+i]=true;if(i)r.parent[68+i]=68+source[i].parent;}
			std::vector<const assembly_attachment*> items;if(optic>=0)items.push_back(&rifle_attachments::common[optic]);if(silenced)items.push_back(&rifle_attachments::common[0]);if(reverse)std::reverse(items.begin(),items.end());
			for(auto* item:items)
			{
				int parent=-1;for(int n=68;n<80;++n)if(bones[n].name==item->contract.receiver_parent)parent=n;
				const int start=r.count;models.push_back({item->contract.model,start,item->bones});
				for(int n=0;n<item->bones;++n){bones[start+n].name=n ? "attachment_child" : item->contract.root;r.parent[start+n]=n ? start : parent;r.weapon_bones[start+n]=true;}
				if(!item->contract.muzzle.empty()){bones[start+1].name=item->contract.muzzle;bones[start+1].bind.position={6,0,0};}r.count+=item->bones;
			}
			for(int n=0;n<r.count;++n){bones[n].parent=r.parent[n];bones[n].bind.rotation={0,0,0,1};}
			const auto span=std::span<const bone_definition>(bones.data(),r.count);const auto selected=select_profile(models,r,span);
			check(selected.value==&profile && (selected.muzzle>=0)==silenced,"M1014 whole assembly retains skin and muzzle authority");
			check(bind_weapon_poses(r,span,profile).valid && tube::bind_parts(r,span).valid,"M1014 source fingers, lifter, bolt and individual shell bind");
			check(!profile.reload && !profile.cylinder && profile.tube,"M1014 never borrows a detachable magazine or cylinder feed");
			const auto& p=*profile.tube;
			check(p.rack_grips.size()==2 && p.interaction.rack.slide_pose_count==2,"M1014 exposes two selectable charging styles");
			check(p.shell_fingers.data()==spas12::feed.shell_fingers.data(),"M1014 and SPAS share the complete shell grasp");
			const vec m1014_shell_bounds_center{.06645646f,.02428348f,.02130692f};
			const vec spas_shell_bounds_center{-.00591090f,-.01565551f,-.00467657f};
			const auto held_center=vr::gameplay::hands::pose_math::compose(p.shell_in_wrist,{m1014_shell_bounds_center,{0,0,0,1}}).position;
			const auto source_center=vr::gameplay::hands::pose_math::compose(spas12::feed.shell_in_wrist,{spas_shell_bounds_center,{0,0,0,1}}).position;
			check(length(sub(held_center,source_center))<.00001f,"M1014 shell model origin is rebased to the same physical grip as SPAS");
			for(const auto& joint:p.shell_fingers)
				check(std::count_if(profile.fingers.begin(),profile.fingers.end(),[&](const auto& item){return item.name==joint.name;})==1,"all shared shell finger chains bind on M1014");
			check(p.rack_grips.front().fingers.data()==hand_poses::edge_handle::index_fingers.data() && p.rack_grips.front().fingers.size()==18,
				"M1014 left hand uses the complete shared AK index grasp");
			for(const auto& joint:p.rack_grips.front().fingers)
			{
				check(std::count_if(profile.fingers.begin(),profile.fingers.end(),[&](const auto& item){return item.name==joint.name;})==1,
					"every borrowed charging finger binds once on the M1014 glove");
				check(std::count_if(p.rack_grips.front().fingers.begin(),p.rack_grips.front().fingers.end(),[&](const auto& item){return item.name==joint.name;})==1,
					"borrowed fingers never duplicate side-name substitutions");
			}
			for(float travel:{0.f,p.interaction.rack.locked_travel,p.interaction.rack.slide_stroke})
			{
				const auto offset=scale(p.interaction.rack.slide_axis,travel*39.37007874f);
				for(const auto& style:p.rack_grips)for(bool mirror:{false,true})
				{
					const auto pose=mirror ? vr::gameplay::hands::pose_mirror::part(style,{1,0,0,0}) : style;
					auto wrist=pose.wrist;wrist.position=add(wrist.position,offset);
					check(choose_part_grip({&pose,1},wrist,offset,p.rack_low,p.rack_high,39.37007874f).distance_meters<.001f,
						"borrowed M1014 grasp stays on its handle through the complete stroke for either hand");
				}
			}
			const auto point=vr::gameplay::hands::pose_math::compose(p.rack_grips.front().wrist,{p.rack_grips.front().contact_in_wrist,{0,0,0,1}}).position;
			const auto mirrored=vr::gameplay::hands::pose_mirror::part(p.rack_grips.front(),{1,0,0,0});
			check(length(sub(point,vr::gameplay::hands::pose_math::compose(mirrored.wrist,{mirrored.contact_in_wrist,{0,0,0,1}}).position))<.001f && point[1]<0,"mirrored M1014 hand still grasps real right-side bolt handle");
			check(length(sub(p.port_center,p.tube_center))*.0254f>p.interaction.port_radius+p.interaction.tube_radius,"side and bottom insertion contact spheres do not overlap");
			++count;
		}
		std::cout<<"M1014 assembly combinations checked: "<<count<<'\n';return errors;
	}
}
