#pragma once
#include "native_fx_checkpoint_policy.hpp"

namespace vr::gameplay::native_fx::checkpoint
{
	bool initialize();
	bool publish(std::span<game::FxEffectDef* const>);
	void remove(variant);
	std::string status();
}
