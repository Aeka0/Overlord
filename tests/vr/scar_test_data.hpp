#pragma once
#include <array>
#include <string_view>
namespace scar_test_data
{
struct bone { std::string_view name; int parent; };
// h2_viewmodel_scar_h_base SHA-256 9062f1d89ff16e6a57afb0c77c86bd097b27a0d81a0a8956894f35816afa78cf
inline constexpr std::array<bone,19> receiver{{
{"j_gun",-1},
{"j_front_ring",0},
{"j_reload",0},
{"j_trigger",0},
{"tag_acog_2",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_eotech",0},
{"tag_flash",0},
{"tag_foregrip",0},
{"tag_heartbeat",0},
{"tag_m203",0},
{"tag_red_dot",0},
{"tag_shotgun",0},
{"tag_sight_off",0},
{"tag_sight_on",0},
{"tag_silencer",0},
{"tag_thermal_scope",0},
{"j_bullets",6},
}};
// attach_h2_shotgun_vm SHA-256 9ae6cbb7045776bcd5713d275dc32e9d47c0fce0ae74fc4ad689e3feb0ee6651
inline constexpr std::array<bone,5> shotgun{{
{"tag_shotgun",-1},
{"j_ammo_shotgun",0},
{"j_plate_shotgun",0},
{"j_pump_shotgun",0},
{"j_reload_shotgun",0},
}};
// attach_h2_m203_vm SHA-256 da2a633786448720c99563882671daf5295eabccf60caafbc88823814a0d8521
inline constexpr std::array<bone,7> launcher{{
{"tag_m203",-1},
{"j_grenade_m203",0},
{"j_m203_button",0},
{"j_slider_m203",0},
{"j_trigger_m203",0},
{"j_grenade_main",1},
{"j_grenade_shell",1},
}};
}
