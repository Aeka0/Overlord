#pragma once
#include <cstdint>
#include <string_view>
#include "official_cheats_policy.hpp"

namespace vr::gameplay::cheats
{
    // Consume native effective state shared by official menus and launcher requests.
    bool sustain_ammo() noexcept;
    bool ragdoll_impact() noexcept; // Native effective level state, queried on the server at contact.
    bool green_beret() noexcept;
    bool transitioning() noexcept;
    bool chest_bayonet() noexcept;
    bool waist_pickaxes() noexcept;
    bool body_weapon(std::string_view name) noexcept;
    // Server inventory/interaction boundary, before admitting a new frame.
    bool update();
    bool settle_pickup(std::uint32_t weapon);
}
