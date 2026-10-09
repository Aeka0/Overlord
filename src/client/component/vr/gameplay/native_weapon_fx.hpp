#pragma once
#include "weapon_feedback.hpp"
#include <string>
namespace game {struct FxEffectDef;}

namespace vr::gameplay::weapons::native_weapon_fx
{
	bool initialize() noexcept;
	game::FxEffectDef* restore_definition(game::FxEffectDef*) noexcept;
	// Main/client boundary only, after committed-shot feedback admission.
	void play(const feedback::event&) noexcept;
	// Native frontend scene submission only (not a backend/eye callback).
	// The caller has an accepted shot and current scene/model ownership.
	bool play_frontend(game::FxEffectDef*,const hands::anchor&,int start_time=-1) noexcept;
	// Same frontend ownership; shell FX use retained world-depth descriptors.
	bool play_shell_frontend(game::FxEffectDef*,const hands::anchor&) noexcept;
	std::string status();
}
