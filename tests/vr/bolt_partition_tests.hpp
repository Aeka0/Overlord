#pragma once
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/rigid_part_visibility.hpp"
#include <iostream>
#include <limits>

namespace bolt_partition_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		int failed{};const auto check=[&](bool ok,const char* message){if(!ok){++failed;std::cerr<<"FAIL: "<<message<<'\n';}};
		for(const auto* p:{&m16::physical,&fal::physical,&mp5::physical,&mp5::arctic,&ump::physical,&ump::arctic,&ump::digital})
		{
			check(p->bolt_partition && valid_partition_profile(*p),"reviewed rigid bolt recipe validates with its actual manual profile");
			const auto& b=*p->bolt_partition;const auto& mesh=b.mesh;
			check(std::string_view(mesh.source)==p->rigid_magazine_source,"each camouflage uses its own native receiver identity");
			unsigned selected{};for(const auto& r:mesh.moving_faces)selected+=r.last-r.first+1;
			check(selected==(p==&m16::physical?134u:p==&fal::physical?124u:p->native_name=="mp5"?75u:228u),
				"partition includes the reviewed bolt component only, not receiver wall or handle");
			check(displayed_internal_bolt(*p,0,0,false,.02f)>.05f && displayed_internal_bolt(*p,0,0,false,.1f)==0,
				"a static source fire clip still gives a visible independent bolt cycle and closed rest");
			check(displayed_internal_bolt(*p,0,0,true,1)==b.motion.locked_m &&
				displayed_internal_bolt(*p,p->interaction.slide_stroke,0,false,-1)==b.motion.travel.back().bolt_m,
				"bolt retains its own catch when the handle returns, and reaches the stop during a full manual pull");
			for(float amount:{0.f,.5f,1.f})for(float units:{1.f,39.37007874f})
			{
				const auto pose=partition_bolt_pose(*p,p->interaction.slide_stroke*amount,false,-1,units);
				check(length(sub(pose.position,b.motion.rest.position))<=b.motion.travel.back().bolt_m*units+.0001f &&
					pose.rotation==b.motion.rest.rotation,"internal bolt translates without inheriting HK handle lift or exceeding the stop");
			}
			auto invalid=*p;auto bad=b;invalid.bolt_partition=&bad;
			bad.motion.bone="tag_clip";check(!valid_partition_profile(invalid),"magazine cannot become a partition source");
			bad=b;bad.motion.shot_stroke_m=0;check(!valid_partition_profile(invalid),"static source requires explicit authored fire travel");
			bad=b;bad.mesh.moving_low[0]=std::numeric_limits<float>::quiet_NaN();check(!valid_partition_profile(invalid),"invalid geometry witness rejects before native asset access");
			bad=b;bad.mesh.source="different_receiver";check(!valid_partition_profile(invalid),"a partition from another receiver or skin cannot enter this profile");
			bad=b;invalid.bolt=&b.motion;check(!valid_partition_profile(invalid),"bone and polygon bolt drivers cannot be enabled together");
			invalid=*p;invalid.handle_fold=&fn2000::handle_fold;check(!valid_partition_profile(invalid),"one source partition cannot be claimed by two presenters");
			std::array<scene_models::surface_face_range,2> overlapping{{mesh.moving_faces[0],mesh.moving_faces[0]}};
			auto broken=mesh;broken.moving_faces=overlapping;check(!valid_partition(broken),"overlapping face recipes reject before any index slicing");
		}
		// Replacing j_gun is an exact rigid group mask, not subtree hiding. The
		// magazine and attachment groups remain in the native skinned model.
		part_mask hidden{};hidden[2]=0x80000000u>>4;
		constexpr std::array<rigid_group_range,3> groups{{{0,3,0,1},{64,3,1,1},{128,3,2,1}}};
		const auto plan=plan_rigid_visibility(groups,68,9,3,hidden);
		check(plan.valid && plan.hidden_groups==1,"receiver partition hides only the selected rigid group and preserves its sibling groups");
		check(valid_partition_profile(fn2000::physical) && partition_mesh(fn2000::physical)==fn2000::handle_fold.mesh,
			"F2000 folding tip continues through the same shared mesh partition contract");
		return failed;
	}
}
