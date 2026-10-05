#pragma once
#include <string_view>
namespace vr::gameplay::cheats
{
    inline constexpr std::string_view script_name="scripts/vr_official_cheats";
    inline constexpr std::string_view script_source=R"GSC(
// The request lives in the VM, so a checkpoint can resume either wait safely.
greenberet_giveweapon()
{
    while (isdefined(level.vr_cheat_transition)) wait 0.05;
    level.vr_cheat_transition = 1;
    while (isdefined(level.vr_cheat_transition)) wait 0.05;
}
greenberet_takeweapon()
{
    while (isdefined(level.vr_cheat_transition)) wait 0.05;
    level.vr_cheat_transition = 2;
    while (isdefined(level.vr_cheat_transition)) wait 0.05;
}
greenberet_monitor()
{
    // VR settles reserve on the accepted physical pickup, even without a native
    // weapon_change. Retain the separate mission flare watcher.
    vrcheatflaremonitor();
}
declare_saved_fields()
{
    level.vr_cheat_inventory = undefined;
    level.vr_cheat_phase = undefined;
}
)GSC";
}
