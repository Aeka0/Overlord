#pragma once
#include "free_climb.hpp"

namespace vr::gameplay::free_climb
{
    // Only world contact is durable. Tracking baselines, withdrawal gestures,
    // pending body motion and hardware reference IDs must never cross a load.
    struct saved_support {vec point_units{},normal{};quat rotation{0,0,0,1};bool valid{};};
    inline std::array<saved_support,2> checkpoint(const solver& state,float units) noexcept
    {
        std::array<saved_support,2> out{};
        for(unsigned h=0;h<2;++h)if(state.hands[h].surface.valid)
            out[h]={scale(state.hands[h].surface.point,units),state.hands[h].surface.normal,state.hands[h].rotation,true};
        return out;
    }
    inline bool restore(solver& state,const std::array<saved_support,2>& saved,float units) noexcept
    {
        if(!std::isfinite(units) || units<.01f || units>10000)return false;
        for(const auto& h:saved)if(h.valid && (!finite(h.point_units) || !finite(h.normal) ||
            std::abs(length(h.normal)-1.f)>.01f || !rotation_valid(h.rotation)))return false;
        solver next;
        for(unsigned h=0;h<2;++h)if(saved[h].valid)
            next.seed(h,{scale(saved[h].point_units,1/units),saved[h].normal,true},saved[h].rotation);
        state=next;return true;
    }
}
