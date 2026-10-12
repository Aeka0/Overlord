#pragma once
#include "native_carry.hpp"
#include <string_view>

namespace vr::gameplay::cheats::exo_punch
{
    struct hit_ticket
    {
        weapons::native_carry::world_key target{};
        std::uint64_t serial{};
    };
    bool active() noexcept; // Server contact boundary; requires ready native death hooks.
    hit_ticket begin_hit(weapons::native_carry::world_key target, std::string_view weapon) noexcept;
    void finish_hit(hit_ticket ticket, bool lethal) noexcept;
}
