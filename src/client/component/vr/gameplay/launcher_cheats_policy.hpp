#pragma once
#include <cstdint>

namespace vr::gameplay::cheats::launcher
{
    inline constexpr int reserve_rounds=500;
    inline constexpr unsigned god=1,demigod=2,notarget=4,flag_mask=god|demigod|notarget;
    struct flag_update {unsigned flags{},owned{};};
    // Only remove bits introduced by this launcher. Script/console protection
    // already present at activation remains native-owned when the option is off.
    inline constexpr flag_update apply_flags(unsigned current,unsigned owned,int health,bool hidden) noexcept
    {
        owned&=flag_mask;
        const unsigned wanted=(health==1?demigod:health==2?god:0u)|(hidden?notarget:0u);
        return {(current&~(owned&~wanted))|wanted,(owned|(~current&wanted))&wanted};
    }
}
