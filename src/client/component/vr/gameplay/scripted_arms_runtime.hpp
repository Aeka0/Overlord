#pragma once
#include <string>

namespace vr::gameplay::scripted_arms
{
    bool owns(const void* object) noexcept;
    bool wants_pose(const void* object,const unsigned* requested) noexcept;
    // After the existing hook completes ALL bones, under its native DObj lock.
    void apply(void* object,bool rebuilt) noexcept;
    std::string status();
}
