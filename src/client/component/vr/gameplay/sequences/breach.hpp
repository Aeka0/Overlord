#pragma once
#include "../scripted_sequences.hpp"
#include <string_view>
namespace vr::gameplay::sequences::breach
{
	inline phase classify(bool alive,bool linked,std::string_view rig,bool shooting) noexcept
	{
		if(!alive || !linked || (rig!="h2_active_breacher_rig" && rig!="active_breacher_rig" && rig!="passive_breacher_rig"))return phase::none;
		return shooting ? phase::breach_combat : phase::breach_plant;
	}
	phase observe();
	inline view presentation(phase stage) noexcept
	{
		view result{};
		if(stage!=phase::breach_plant && stage!=phase::breach_combat)return result;
		result.stage=stage;result.scene=scenario::breach;
		result.camera=scene_cameras::breach;
		result.suspend_weapons=stage==phase::breach_plant;
		result.allow_movement=result.allow_turn=stage==phase::breach_combat;
		return result;
	}
}
