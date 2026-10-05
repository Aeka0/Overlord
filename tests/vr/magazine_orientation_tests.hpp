#pragma once
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/famas/profile.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/weapons/m82/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/p90/profile.hpp"
#include "component/vr/gameplay/weapons/rpd/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/magazine_grasp_profile.hpp"
#include "component/vr/gameplay/magazine_grip_selection.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include <limits>

namespace magazine_orientation_tests
{
	template<class Check>void run(Check check)
	{
		namespace w=vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		const quat mirror{1,0,0,0},ordinary=normalize({.3f,.2f,-.1f,.8f});
		for(const auto* p:{&w::scar::physical,&w::cheytac::physical,&w::cheytac::desert,&w::m82::physical,
			&w::m14ebr::physical,&w::m14ebr::arctic,&w::fn2000::physical,
			&w::famas::physical,&w::famas::tape,&w::famas::woodland,
			&w::tavor::physical,&w::tavor::digital,&w::tavor::woodland,
			&w::mp5::physical,&w::mp5::arctic,&w::ump::physical,&w::ump::arctic,&w::ump::digital})
		{
			check(p->magazine_tracking==w::magazine_tracking_frame::controller && p->magazine_selection==w::magazine_grasp_policy::fixed,
				"adopted families and cosmetic variants use the common controller magazine policy");
			for(int actor=0;actor<2;++actor)for(auto rotation:{quat{0,0,0,1},normalize(quat{.4f,.1f,-.2f,.7f}),normalize(quat{-.2f,.5f,.3f,.6f})})
			{
				const auto grasp=w::select_magazine_grip(*p,ordinary,actor,mirror,false,false,255,rotation);
				const auto held=w::select_magazine_grip(*p,ordinary,actor,mirror,false,true,grasp.index,conjugate(rotation));
				const auto seated=w::select_magazine_grip(*p,ordinary,actor,mirror,false,false,255,rotation,true);
				check(grasp.index==p->magazine_default_pose && held.index==grasp.index && seated.index==grasp.index &&
					grasp.index<p->interaction.magazine_pose_count && held.fingers.data()==grasp.fingers.data(),
					"free, acquired and receiver-held magazines keep one coherent recipe through controller rotation");
				check(p->magazine_grasps.empty() ? grasp.fingers.data()==p->magazine_fingers.data() :
					p->magazine_grasps[grasp.index].kind==w::magazine_grasp_kind::body_wrap,
					"policy reuses a fitted wrap when available and the original anatomical hand otherwise");
				const auto final=multiply(multiply(rotation,w::magazine_wrist_basis(*p,grasp,ordinary,false)),grasp.in_wrist.rotation);
				for(auto axis:{vec{1,0,0},vec{0,1,0},vec{0,0,1}})
					check(dot(rotate(final,axis),rotate(rotation,axis))>.99999f,"both hands align all magazine axes to tracked controller axes");
			}
			const auto twice=w::with_controller_magazine(*p);
			check(twice.magazine_default_pose==p->magazine_default_pose && twice.magazine_in_wrist.position==p->magazine_in_wrist.position &&
				twice.magazine_contacts==p->magazine_contacts && twice.magazine_grasps.data()==p->magazine_grasps.data() &&
				twice.magazine_top==p->magazine_top && twice.well.position==p->well.position &&
				twice.rigid_magazine_source==p->rigid_magazine_source && twice.interaction.well_radius==p->interaction.well_radius,
				"idempotent profile policy preserves model geometry, contacts, scales and well tolerances");
		}
		const auto unchanged=[&](const w::reload_profile& p) {
			const auto result=w::with_controller_magazine(p);
			return result.magazine_tracking==p.magazine_tracking && result.magazine_selection==p.magazine_selection &&
				result.magazine_default_pose==p.magazine_default_pose && result.interaction.magazine_pose_count==p.interaction.magazine_pose_count;
		};
		for(const auto* p:{&w::p90::physical,&w::m9::physical,&w::rpd::physical})
			check(unchanged(*p),"horizontal/facing, pistol catch and belt profiles cannot accidentally enter the box-magazine policy");
		auto incomplete=w::mp5::physical;incomplete.magazine_tracking=w::magazine_tracking_frame::weapon_wrist;
		incomplete.magazine_selection=w::magazine_grasp_policy::wrist_facing;incomplete.magazine_contacts=nullptr;
		check(unchanged(incomplete),"missing contact resources leave the whole legacy configuration intact");
		incomplete.magazine_contacts=w::mp5::physical.magazine_contacts;
		incomplete.magazine_in_wrist.rotation={0,0,0,0};check(unchanged(incomplete),"degenerate grip frames cannot partially enable controller alignment");
		incomplete.magazine_in_wrist=w::mp5::physical.magazine_in_wrist;
		incomplete.magazine_in_wrist.position[0]=std::numeric_limits<float>::quiet_NaN();check(unchanged(incomplete),"nonfinite offsets cannot enter aligned magazine policy");
		std::array<w::magazine_grasp_pose,w::max_part_grips+1> oversized{};
		incomplete.magazine_grasps=oversized;check(unchanged(incomplete),"oversized authored recipe sets are rejected before indexing");
		// Special equipment bypass remains effective even if paired with an opted-in profile.
		auto knife=w::scar::physical;knife.knife_magazine_in_wrist=w::m9::physical.knife_magazine_in_wrist;
		const auto grasp=w::select_magazine_grip(knife,ordinary,0,mirror,true,false,255);
		check(grasp.in_wrist.position==knife.knife_magazine_in_wrist->position && w::magazine_wrist_basis(knife,grasp,ordinary,true)==ordinary,
			"knife co-grasps retain priority and their authored wrist frame");
		const auto legacy=w::select_magazine_grip(w::p90::physical,ordinary,0,mirror,false,false,0);
		check(w::magazine_wrist_basis(w::p90::physical,legacy,ordinary,false)==ordinary,"unadopted horizontal magazines retain legacy tracking");
		check(w::m14ebr::physical.matches_native("m21",10) && w::m14ebr::physical.matches_native("m14",10),"M21 and M14 retain the shared native admission");
	}
}
