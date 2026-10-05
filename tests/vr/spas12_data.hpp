#pragma once
#include <array>
#include <string_view>
namespace spas12_data { struct bone{std::string_view name;int parent;};
// ebd6a59b4882e2ae40aeb04c2b75ce9579e38ee3b59a6637b2f69ca4a2341d42
inline constexpr std::array<bone,17> bones{{
{"j_gun",-1},
{"j_pump",0},
{"j_reload",0},
{"j_reload_plate",0},
{"j_ring_back",0},
{"j_ring_start",0},
{"j_stock",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_eotech",0},
{"tag_flash",0},
{"tag_heartbeat",0},
{"tag_red_dot",0},
{"tag_sight_on",0},
{"tag_silencer",0},
{"j_ring_end",5},
{"tag_foregrip",1},
}};}
