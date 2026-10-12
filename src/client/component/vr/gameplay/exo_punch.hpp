#pragma once
#include "native_carry.hpp"

namespace vr::gameplay::cheats::exo_punch
{
    bool active() noexcept; // Server contact boundary; requires ready native death hooks.
    // After accepted lethal G_Damage, bind the claim to native death attribution.
    // A fist/body knife borrows a damage definition without owning that weapon.
    void record_hit(weapons::native_carry::world_key target) noexcept;
}
