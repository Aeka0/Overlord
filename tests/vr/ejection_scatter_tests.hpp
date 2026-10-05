#pragma once
#include "component/vr/gameplay/ejection_scatter.hpp"
#include "component/vr/gameplay/physical_reload_geometry.hpp"

namespace ejection_scatter_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::hands;
		namespace w=vr::gameplay::weapons;
		const anchor exit{{3,4,5},{0,0,.70710678f,.70710678f}};
		const auto close=[](vec a,vec b){return length(sub(a,b))<.0001f;};
		for(unsigned n=0;n<6;++n)
		{
			const auto scatter=w::ejection_scatter::sample(17,8,n);
			const auto same=w::ejection_scatter::sample(17,8,n),next=w::ejection_scatter::sample(17,9,n);
			check(scatter.velocity==same.velocity && scatter.angular_velocity==same.angular_velocity,
				"cartridge randomness stays fixed across eyes and repeated render samples");
			check(scatter.velocity!=next.velocity && scatter.velocity!=w::ejection_scatter::sample(18,8,n).velocity,
				"new ejection event and weapon lifetime receive independent scatter");
			check(std::abs(scatter.velocity[0])<=.035f && std::abs(scatter.velocity[1])<=.12f && std::abs(scatter.velocity[2])<=.10f,
				"scatter remains a small bounded disturbance of the authored exit");
			for(float age:{0.f,.03f,.06f})
			{
				const auto base=w::physical_reload::free_drop(exit,{0,0,-15},age,100);
				const auto moved=w::ejection_scatter::apply(base,exit.rotation,scatter,age,100);
				check(moved.position==base.position && moved.rotation==base.rotation,"round clears cylinder or breech before scatter begins");
			}
			const auto moved=w::ejection_scatter::apply(exit,exit.rotation,scatter,.3f,100);
			const auto other=w::ejection_scatter::apply(exit,exit.rotation,w::ejection_scatter::sample(17,8,n+1),.3f,100);
			check(!close(moved.position,other.position),"each ejected round separates from its neighbours");
			const anchor tip{{1,0,0},{0,0,0,1}};
			const auto round_tip=vr::gameplay::hands::pose_math::compose(moved,tip);
			check(std::abs(length(sub(round_tip.position,moved.position))-1)<.0001f,
				"live bullet tip remains rigidly attached while its cartridge tumbles");
			const auto metres=w::ejection_scatter::apply({},exit.rotation,scatter,.3f,1);
			check(close(sub(moved.position,exit.position),scale(metres.position,100)),"scatter respects model unit scale");
		}
	}
}
