#pragma once
#include <cstdint>
#include <string_view>

namespace vr::gameplay::cheats::exo_punch
{
    struct impact_claim
    {
        int time{};
        std::string_view weapon;
    };
    struct death_evidence
    {
        int time{}, health{}, means_of_death{};
        bool enabled{}, official_ragdoll{}, ragdolls{}, player_attacker{}, forced_ragdoll{};
        std::string_view weapon, classname;
    };
    // A claim comes only from accepted lethal VR melee and freezes its observed
    // native attribution (including "none" for an empty-handed player). Death
    // evidence must still identify that same player melee, never a later bullet,
    // explosion, football impact or scripted death. Bound delayed/stale claims.
    inline constexpr bool admits(impact_claim claim, death_evidence death) noexcept
    {
        const auto age=std::int64_t(death.time)-claim.time;
        return death.enabled && !death.official_ragdoll && death.ragdolls &&
            death.health<=0 && death.player_attacker && death.means_of_death==8 &&
            !death.forced_ragdoll && age>=0 && age<=1000 &&
            !claim.weapon.empty() && claim.weapon==death.weapon && death.weapon!="h2_cheatfootball" &&
            death.classname.find("actor_civilian_slum_")==std::string_view::npos;
    }
}
