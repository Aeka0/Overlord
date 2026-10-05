#pragma once
#include "../scripted_sequences.hpp"

namespace scripting {class entity;}

namespace vr::gameplay::sequences::sliding
{
	inline view classify(bool alive,bool linked,unsigned parent,unsigned model)noexcept
	{
		if(!alive || !linked || !parent || parent!=model)return {};
		auto result=free_look(scenario::sliding);result.camera=scene_cameras::sliding;return result;
	}
	// The coordinator already established a live linked player. Only the
	// common native slidemodel relation is read here; no map-name catalog.
	view observe(const scripting::entity& player,const scripting::entity& parent);
}
