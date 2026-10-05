#pragma once
#include "hands/pose_solver.hpp"
#include <cstdint>

namespace vr::gameplay::native_followed_fx
{
    inline bool valid_pose(hands::anchor pose) noexcept
    {
        for(float x:pose.position)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
        float norm{};for(float x:pose.rotation){if(!std::isfinite(x))return false;norm+=x*x;}
        return std::abs(norm-1)<.01f;
    }
    struct state
    {
        hands::anchor pose{};std::uint64_t activation{};int started{};bool active{};
        bool begin(std::uint64_t id,int time,hands::anchor value) noexcept
        {
            if(!id || time<0 || !valid_pose(value) || activation==id)return false;
            *this={value,id,time,true};return true;
        }
        void position(std::uint64_t id,hands::anchor value) noexcept
        {if(activation==id && valid_pose(value))pose=value;}
        bool follows(int birth)const noexcept{return active && started==birth;}
    };
}
