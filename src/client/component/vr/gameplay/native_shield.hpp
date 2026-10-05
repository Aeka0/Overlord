#pragma once
#include "weapon_identity.hpp"
#include "hands/pose_solver.hpp"
namespace vr::gameplay::weapons::shield
{
 bool enabled() noexcept;
 bool firing_clear(weapon_identity weapon,hands::vec muzzle) noexcept;
}
