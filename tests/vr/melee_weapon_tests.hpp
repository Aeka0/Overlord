#pragma once
#include "component/vr/gameplay/melee_weapon_profile.hpp"
#include "component/vr/gameplay/melee_motion.hpp"
#include <limits>

namespace melee_weapon_tests
{
	template<class Check>void run(Check check)
	{
		namespace m=vr::gameplay::equipment::melee_weapons;
		namespace e=vr::gameplay::equipment;
		using namespace vr::gameplay::hands;
		using namespace vr::gameplay::hands::pose_math;
		check(m::resolve(m::choice::native,false)==m::kind::ghost && m::resolve(m::choice::native,true)==m::kind::bayonet,
			"Default retains Ghost Dance and native Green Beret substitution");
		for(auto selected:{m::choice::ghost,m::choice::baton,m::choice::karambit,m::choice::ending,m::choice::bloody})
			check(!m::follows_cheats(selected) && m::resolve(selected,true)==m::resolve(selected,false),
				"Every named selection, including Ghost Dance, overrides Green Beret melee tools");
		for(int invalid:{-1,6,100,std::numeric_limits<int>::max()})
			check(m::sanitize(invalid)==m::choice::native,"Malformed saved enum cannot select an arbitrary model");
		for(auto requested:{e::knife_grip::forward,e::knife_grip::reverse})
		{
			check(m::grip(m::kind::baton,requested)==e::knife_grip::forward,"Baton always uses forward grip");
			check(m::grip(m::kind::karambit,requested)==e::knife_grip::reverse,"Karambit always uses reverse grip");
		}
		for(auto variant:{m::kind::ghost,m::kind::ending,m::kind::bloody})for(bool right:{false,true})
		{
			const auto& p=m::get(variant);
			const auto f=m::attachment(variant,e::knife_grip::forward,{0,0,0,1},right);
			const auto r=m::attachment(variant,e::knife_grip::reverse,{0,0,0,1},right);
			check(length(sub(compose(f,{p.grip_center,{0,0,0,1}}).position,compose(r,{p.grip_center,{0,0,0,1}}).position))<.0001f,
				"Forward/reverse changes keep each selected handle at its own hand pivot");
		}
		const auto collapsed=m::baton_pose(0,false),extended=m::baton_pose(1,true);
		check(extended[8].position[2]-collapsed[8].position[2]>10.f,
			"Baton shaft retains its measured native collapsed-to-extended travel");
		check(m::baton_pose(100,false)[8].position==collapsed[8].position,"Stowed baton is always collapsed");
		check(m::baton_pose(std::numeric_limits<float>::quiet_NaN(),true)[8].position==collapsed[8].position,
			"Invalid animation clock cannot publish nonfinite skeletal coordinates");
		for(float seconds:{0.f,.1f,.25f,.5f,1.f})
			for(const auto& bone:m::baton_pose(seconds,true))
				check(std::abs(bone.rotation[0]*bone.rotation[0]+bone.rotation[1]*bone.rotation[1]+
					bone.rotation[2]*bone.rotation[2]+bone.rotation[3]*bone.rotation[3]-1.f)<.001f,"Every deployment bone keeps a unit rotation");
		using namespace vr::gameplay::melee;
		for(bool boost:{false,true})for(int base:{50,200,350})
			check(resolved_damage(base,tool::knife,boost,350,{m::fixed_damage(m::kind::baton),false})==120,
				"Baton damage remains 120 even under native Ragdoll Impact or EXO boost");
		check(!m::blade_feedback(m::kind::baton) && m::blade_feedback(m::kind::karambit),
			"Baton has no blade screen-blood path; bladed tools retain native feedback");
		check(!resolved_damage(-1,tool::knife,true,350,{120,false}) && !resolved_damage(200,tool::fist,false,0,{120,false}),
			"Damage override cannot bypass invalid providers or leak into empty-hand strikes");
	}
}
