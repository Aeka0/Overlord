#pragma once
#include "free_climb.hpp"

namespace vr::gameplay::free_climb
{
    // A free route may pass beside or above the authored animation start.
    // Keep the horizontal handoff local, but never require descending back
    // into a small sphere after crossing the ledge height. Coordinates: metres.
    inline bool reached_exit(vec position,vec finish) noexcept
    {
        if(!finite(position) || !finite(finish))return false;
        const auto delta=sub(position,finish);
        return delta[2]>=-.4f && delta[0]*delta[0]+delta[1]*delta[1]<=1.25f*1.25f;
    }
}
