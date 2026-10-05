#pragma once
#include "../controller_input.hpp"
#include "weapon_holding.hpp"
namespace vr::gameplay::weapon_hud
{
	// Command-owner only; presentation takes bounded snapshots, never consumes input.
	void update(const controller_input::frame&, const weapons::hold&, bool gameplay,
		controller_input::clock::time_point now) noexcept;
	void suspend() noexcept; // Rearm input only; never changes the visibility latch.
}
