#pragma once
#include <array>
#include <string_view>
namespace winchester1200_data { struct bone{std::string_view name;int parent;};
// ec49350ee8ecbd007a750e830ceed6979c9e0dcf57194c93871706870fa83b64
inline constexpr std::array<bone,16> bones{{
{"j_gun",-1},
{"j_load",0},
{"j_pump",0},
{"j_slide",0},
{"j_trigger",0},
{"tag_ak47_mount",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_flash",0},
{"tag_holo",0},
{"tag_iron_sight",0},
{"tag_thermal_scope",0},
{"tag_foregrip",2},
{"tag_foregrip_rail",2},
{"tag_sight_off",10},
{"tag_sight_on",10},
}};}
