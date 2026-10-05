#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "estate_variant_data.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/tube_profiles.hpp"
#include "component/vr/gameplay/tube_presenter.hpp"
#include "component/vr/gameplay/underbarrel_rig.hpp"
#include <iostream>
#include <vector>

namespace estate_variant_tests
{
	inline int run()
	{
		using namespace vr::gameplay::hands;
		using namespace vr::gameplay::weapons;
		int failures{};
		for(const auto& sample:estate_variant_data::variants)
		{
			const auto check=[&](bool ok,const char* why) {
				if(!ok){++failures;std::cerr<<"FAIL: "<<sample.native<<": "<<why<<'\n';}
			};
			const auto& p=*sample.expected;
			rig r{};r.parent.fill(-1);r.gun=68;r.count=68;r.arms[0].wrist=0;r.arms[1].wrist=1;
			std::array<bone_definition,256> bones{};
			for(int i=0;i<68;++i)bones[i].bind.rotation={0,0,0,1};
			for(size_t i=0;i<p.fingers.size();++i)
			{
				bones[2+i].name=p.fingers[i].name;
				r.parent[2+i]=bones[2+i].name.find("_le")!=std::string_view::npos?0:1;
			}
			std::vector<model_definition> models{{"viewhands_us_army",0,68}};
			for(const auto& model:sample.models)
			{
				const int start=r.count;
				int parent=13;
				if(start>68)
				{
					parent=-1;
					for(int i=68;i<models[1].begin+models[1].count;++i)
						if(bones[i].name==model.bones[0].name)parent=i;
				}
				check(parent>=0,"captured attachment root has a receiver mount");
				models.push_back({model.name,start,int(model.bones.size())});
				for(const auto& source:model.bones)
				{
					bones[r.count]=source;
					r.parent[r.count]=source.parent<0?parent:start+source.parent;
					r.weapon_bones[r.count]=true;
					if(start>68 && parent>=0)
					{
						const auto posed=vr::gameplay::hands::pose_math::compose(vr::gameplay::hands::pose_math::as_anchor(bones[parent].bind),vr::gameplay::hands::pose_math::as_anchor(source.bind));
						bones[r.count].bind.position=posed.position;bones[r.count].bind.rotation=posed.rotation;
					}
					++r.count;
				}
			}
			for(int i=0;i<r.count;++i)bones[i].parent=r.parent[i];
			const auto span=std::span<const bone_definition>(bones.data(),r.count);
			check(select_profile(models,r,span).value==&p,"captured native model composition selects exact skin and support pose");
			check(bind_weapon_poses(r,span,p).valid,"existing hand and mechanical rest poses bind captured hierarchy");
			if(p.reload)
			{
				check(native_reload_profile(sample.native,sample.capacity,p.reload)==p.reload,"native definition agrees with scene recipe");
				check(!native_reload_profile(sample.native,sample.capacity+1,p.reload),"wrong capacity remains rejected");
				check(physical_reload::bind_parts(r,span,*p.reload).valid,"magazine, rounds and action bind captured hierarchy");
				check(p.reload->rigid_magazine_source==p.receiver,"detached magazine retains captured receiver skin");
			}
			if(p.tube)
			{
				check(native_tube_profile(sample.native,sample.capacity,p.tube)==p.tube,"native shell feed agrees with scene recipe");
				check(!native_tube_profile(sample.native,sample.capacity+1,p.tube),"shell capacity remains checked");
				check(tube::bind_parts(r,span,p.tube).valid,"pump or fixed drum parts bind captured hierarchy");
			}
			if(p.variant=="gp25" || p.variant=="grenadier")
				check(bool(underbarrel::bind(models,r,span)),"captured underbarrel skeleton binds independently of native name match");
			for(size_t m=2;m<models.size();++m)
			{
				const int root=models[m].begin,saved=r.parent[root];r.parent[root]=0;
				check(!select_profile(models,r,span).value,"known attachment name cannot bypass wrong parent rejection");
				r.parent[root]=saved;
			}
			models[1].name="h2_viewmodel_unreviewed_woodland";
			check(!select_profile(models,r,span).value,"woodland admission is exact, not a suffix wildcard");
		}
		for(auto name:{"tmp_reflex_extra","sa80lmg_scope_extra","dragunov_woodland_extra"})
			if(native_reload_profile(name,32) || native_reload_profile(name,100) || native_reload_profile(name,10))++failures;
		if(native_tube_profile("spas12_silencer",7) || native_tube_profile("spas12_heartbeat",7))++failures;
		std::cout<<"Estate captured assemblies checked: "<<estate_variant_data::variants.size()<<'\n';
		return failures;
	}
}
