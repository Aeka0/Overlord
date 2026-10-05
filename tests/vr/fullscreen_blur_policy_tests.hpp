#pragma once
#include "component/vr/fullscreen_blur_policy.hpp"
#include <limits>
#include <cmath>

template<class Check> void fullscreen_blur_policy_tests(Check check)
{
	using namespace vr::native_fullscreen_blur;
	const script_range damage{1000,1070};
	check(suppress_damage_request(true,true,2,1030,damage) && suppress_damage_request(true,true,2,1060,damage),
		"injury pulse and delayed clear are both excluded; neither can overwrite a story curve");
	for(const auto caller:{999u,1070u,4000u})check(!suppress_damage_request(true,true,2,caller,damage),
		"death and cinematic callers of the same native blur method stay native");
	check(!suppress_damage_request(false,true,2,1030,damage) && !suppress_damage_request(true,false,2,1030,damage) &&
		!suppress_damage_request(true,true,1,1030,damage),"flat play, other entities and different native call contracts pass through");
	check(!suppress_damage_request(true,true,2,1030,{}) && !script_range{1070,1000}.contains(1030) &&
		!script_range{1,std::numeric_limits<std::uintptr_t>::max()}.contains(1030),"unbound, invalid and oversized script ranges cannot suppress a request");
	for(const auto radius:{0.f,.125f,5.f,40.f})
		check(filter_menu_radius(true,radius)==0 && filter_menu_radius(false,radius)==radius,
			"only VR menu contribution is excluded, including its fade tail; flat values pass through");
	for(const auto gameplay:{.125f,20.f,40.f})
	{
		const auto menu=filter_menu_radius(true,5);const float hud=3;
		check(std::sqrt(gameplay*gameplay+hud*hud+menu*menu)==std::sqrt(gameplay*gameplay+hud*hud),
			"native combined radius still includes every death, cinematic and HUD source without a whitelist");
	}
}
