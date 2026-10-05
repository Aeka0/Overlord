#pragma once
#include <string_view>

namespace vr::gameplay::cliffhanger
{
    // Entry is not the current phase. Both consumers additionally consult the
    // native reached_top flag, including when restoring a checkpoint.
    inline bool opening_start(std::string_view start) noexcept
    {return start=="default" || start=="cave" || start=="climb" || start=="jump" || start=="e3";}
}
