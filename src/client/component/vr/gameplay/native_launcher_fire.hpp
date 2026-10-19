#pragma once
#include "shot_geometry.hpp"
namespace game
{
	struct gentity_s;
}
namespace vr::gameplay::weapons::native_ballistics
{
	struct parameters;
}

namespace vr::gameplay::weapons::native_launcher_fire
{
	// Shared native class-6 underbarrel primitive. Ammo and mechanical ownership
	// stay with the caller; the helper only creates the original projectile.
	bool initialize();
	bool prepare(unsigned definition, const shot_geometry&, native_ballistics::parameters&) noexcept;
	game::gentity_s* fire(game::gentity_s*, const native_ballistics::parameters&);
	unsigned resident_grenade() noexcept;
	void reset() noexcept;
}
