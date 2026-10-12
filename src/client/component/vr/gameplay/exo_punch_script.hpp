#pragma once
#include <string_view>

namespace vr::gameplay::cheats::exo_punch
{
    inline constexpr std::string_view script_name="scripts/vr_exo_punch";
    inline constexpr std::string_view script_source=R"GSC(
exo_punch_death()
{
    // The guarded hook admits only our confirmed player melee. Reuse the
    // original helper for weapon drops, upward bias, velocity and ragdoll start.
    // Its impulse inputs are consumed before its final wait; restore them here.
    previous_scale = self.ragdoll_directionscale;
    previous_location = self.damagelocation;
    if (!isdefined(previous_scale))
        self.ragdoll_directionscale = clamp(int(self.damagetaken / 100) * randomintrange(2500, 5000), 2500, 25000);
    if (self.damageweapon == "riotshield")
        self.damagelocation = "torso_upper";
    vrexopunchdeath(self);
    self.ragdoll_directionscale = previous_scale;
    self.damagelocation = previous_location;
    return 1;
}
)GSC";
}
