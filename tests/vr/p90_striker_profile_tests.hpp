#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/p90/profile.hpp"
#include "component/vr/gameplay/weapons/striker/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/tube_presenter.hpp"
#include "component/vr/gameplay/tube_profiles.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"
#include "p90_striker_data.hpp"
#include <vector>
namespace p90_striker_profile_tests
{
	template<class Check>void run(Check check)
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		struct fixture
		{
			rig r{};std::array<bone_definition,256> bones{};std::vector<model_definition> models;
			fixture(const profile& p,std::span<const bone_definition> source)
			{
				r.parent.fill(-1);r.gun=68;r.count=68+int(source.size());r.arms[0].wrist=0;r.arms[1].wrist=1;
				models={{"viewhands_us_army",0,68},{p.receiver,68,int(source.size())}};
				for(size_t i=0;i<p.fingers.size();++i){bones[2+i].name=p.fingers[i].name;r.parent[2+i]=p.fingers[i].name.find("_le")!=std::string_view::npos ? 0 : 1;}
				for(size_t i=0;i<source.size();++i){bones[68+i]=source[i];r.parent[68+i]=i ? 68+source[i].parent : 13;r.weapon_bones[68+i]=true;}
				for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];if(i<68)bones[i].bind.rotation={0,0,0,1};}
			}
			void attach(const assembly_attachment& a)
			{
				int parent=-1;for(int i=68;i<models[1].begin+models[1].count;++i)if(bones[i].name==a.contract.receiver_parent)parent=i;
				const auto start=r.count;models.push_back({a.contract.model,start,a.bones});
				for(int i=0;i<a.bones;++i){r.parent[start+i]=i ? start : parent;r.weapon_bones[start+i]=true;bones[start+i].name=i ? "attachment_child" : a.contract.root;bones[start+i].bind.rotation={0,0,0,1};}
				if(!a.contract.muzzle.empty()){bones[start+1].name=a.contract.muzzle;bones[start+1].bind.position={6,0,0};}
				r.count+=a.bones;
			}
			profile_match resolve(){return select_profile(models,r,{bones.data(),size_t(r.count)});}
		};
		for(size_t skin=0;skin<2;++skin)for(int optic:{-1,5,6,7,8,9,10})for(bool suppressor:{false,true})
		{
			const auto& p=p90::assemblies[skin];fixture f(p,skin ? p90_striker_data::p90_arctic : p90_striker_data::p90);
			if(optic>=0)f.attach(rifle_attachments::common[optic]);if(suppressor)f.attach(rifle_attachments::common[0]);
			const auto match=f.resolve();check(match.value==&p,"P90 base/arctic and supported optics bind exact assembly");
			const auto parts=physical_reload::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},*p.reload);
			check(parts.valid && parts.slide==69 && parts.magazine==76 && parts.magazine_variant_count==11,"P90 action, top magazine and exclusive contents bind");
			check(bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},p).valid,"P90 finger and mechanical rest poses bind");
			f.r.parent[69]=76;check(!physical_reload::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},*p.reload).valid,"P90 handle cannot become magazine-owned");
		}
		const auto source=p90::action_grips[0];const auto right=vr::gameplay::hands::pose_mirror::part(source,{0,0,0,1});
		const auto contact=vr::gameplay::hands::pose_math::compose(source.wrist,{source.contact_in_wrist,{0,0,0,1}}).position;
		const auto other=vr::gameplay::hands::pose_math::compose(right.wrist,{right.contact_in_wrist,{0,0,0,1}}).position;
		check(contact[1]>0 && other[1]<0 && length(sub(other,vr::gameplay::hands::pose_mirror::position(contact)))<.001f,"P90 mirrored hand reaches actual opposite handle");
		for(auto pose:{source,right})
		{
			const std::array<part_grip_pose,1> poses{pose};const auto selected=choose_part_grip(poses,pose.wrist,{},p90::physical.slide_grab_low,p90::physical.slide_grab_high,39.37007874f);
			check(selected.pose==0 && selected.distance_meters<.001f,"each P90 handle admits its own source contact");
		}
		check(p90::manual_magazine.pull_axis==vec{0,0,1},"P90 magazine extracts upward");
		check(rotate(p90::magazine_well.rotation,{0,0,1})==vec{0,0,-1},"P90 insertion rail points downward");
		const auto mesh=p90::physical.magazine_mesh();
		check(mesh && mesh.subsets==11 && mesh.exclusive_rounds,"P90 supports empty plus ten mutually exclusive content variants");
		for(int rounds=0;rounds<=51;++rounds)
		{
			const auto subset=p90::physical.magazine_subset(rounds);size_t variants{};std::array<bool,16> seen{};
			for(size_t i=0;i<mesh.selected_count(subset);++i){auto n=mesh.selected_index(subset,i);check(n<mesh.count && !seen[n],"P90 subset indices are bounded and unique");seen[n]=true;if(n>mesh.body_count)++variants;}
			check(variants==1 && seen[0] && seen[1] && seen[mesh.body_count]==(rounds>0),"P90 detached magazine retains mechanism and exactly one content mesh");
		}
		for(int optic:{-1,6,7})
		{
			fixture f(striker::base,p90_striker_data::striker);if(optic>=0)f.attach(rifle_attachments::common[optic]);
			check(f.resolve().value==&striker::base,"Striker receiver and available optics bind");
			const auto parts=tube::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},&striker::feed);
			check(parts.valid && parts.bolt==-1 && parts.drum==73 && parts.shell==69,"Striker permanent drum and loose shell are separate with no invented bolt");
			check(bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},striker::base).valid,"Striker original grips and rest hierarchy bind");
			f.r.parent[69]=73;check(!tube::bind_parts(f.r,{f.bones.data(),size_t(f.r.count)},&striker::feed).valid,"Striker held shell cannot be a rotating drum child");
		}
		check(native_reload_profile("p90_arctic",50)==&p90::physical && !native_reload_profile("p90",49),"P90 native fifty-round capacity is enforced");
		for(auto name:striker::names)check(native_tube_shape_supported(name,12,true,1),"captured Striker twelve-round segmented feed is admitted");
		check(!native_tube_shape_supported("striker",12,false,1) && !native_tube_shape_supported("striker",13,true,1),"Striker cannot inherit magazine replacement or plus-one capacity");
	}
}
