#pragma once
#include <array>
#include <string_view>
namespace dragunov_data {struct bone{std::string_view name;int parent;};
inline constexpr std::array<bone,19> receiver{{
{"j_gun",-1},
{"j_bolt",0},
{"j_lever_a",0},
{"j_lever_b",0},
{"j_mag_release",0},
{"j_trigger",0},
{"tag_acog_2",0},
{"tag_ak47_mount",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_dragunov_scope",0},
{"tag_flash",0},
{"tag_heartbeat",0},
{"tag_rail",0},
{"tag_sight_off",0},
{"tag_sight_on",0},
{"tag_silencer",0},
{"tag_thermal_scope",0},
{"tag_bullet_single",9},
}};
inline constexpr std::array<bone,4> scope{{
{"tag_sight_on",-1},
{"tag_scope_ads_off",0},
{"tag_scope_ads_on",0},
{"tag_reticle_attach",2},
}};
inline constexpr std::array<bone,11> gold{{
{"j_gun",-1},
{"j_bolt",0},
{"j_press_rear",0},
{"j_trigger",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_flash",0},
{"tag_reflex",0},
{"tag_silencer",0},
{"tag_bullets",5},
{"j_bullet01",9},
}};
}
