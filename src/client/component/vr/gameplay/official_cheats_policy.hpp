#pragma once
#include <string_view>

namespace vr::gameplay::cheats
{
    // Native 45456::greenberet_choose_weapon. The mission pickaxe is not a bayonet skin.
    inline constexpr bool pickaxe_level(std::string_view map) noexcept {return map=="cliffhanger";}
    inline constexpr std::string_view green_beret_weapon(std::string_view map) noexcept
    {return pickaxe_level(map)?"h2_cheatpickaxe":"h2_cheatcommandoknife";}
    inline constexpr bool body_weapon_definition(std::string_view name) noexcept
    {return name=="h2_cheatpickaxe" || name=="h2_cheatcommandoknife";}
}
