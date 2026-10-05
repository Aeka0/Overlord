#pragma once
#include <string_view>

namespace vr::gameplay::cliffhanger
{
    // Entry is not the current phase. Both consumers additionally consult the
    // native reached_top flag, including when restoring a checkpoint.
    inline bool opening_start(std::string_view start) noexcept
    {return start=="default" || start=="cave" || start=="climb" || start=="jump" || start=="e3";}
}

namespace vr::gameplay::cliffhanger_physical
{
    // 47980::_id_B004 uses material categories, not the visible ice texture.
    // Some authored cliff faces are collision-labelled plaster or rock.
    inline bool native_climb_material(std::string_view name) noexcept
    {return name=="ice" || name=="plaster" || name=="rock" || name=="snow";}
}
