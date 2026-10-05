#pragma once
#include <array>
#include <string_view>
namespace m1014_data {struct bone {std::string_view name;int parent;};
// df14ed7ddee1a2fbc3c598a8ff772323c818cbbd0e01e0e76771281fa912acf8
inline constexpr std::array<bone,12> base{{
{"j_gun",-1},
{"j_bolt",0},
{"j_load",0},
{"j_ring",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_eotech",0},
{"tag_flash",0},
{"tag_foregrip",0},
{"tag_red_dot",0},
{"tag_sight_on",0},
{"tag_silencer",0},
}};
// d77bd93f0ecfaafacbdd29dc0279cfb6f0f9f0ce8a6d9b20c3146cb3b485f5d5
inline constexpr std::array<bone,12> arctic{{
{"j_gun",-1},
{"j_bolt",0},
{"j_load",0},
{"j_ring",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_eotech",0},
{"tag_flash",0},
{"tag_foregrip",0},
{"tag_red_dot",0},
{"tag_sight_on",0},
{"tag_silencer",0},
}};
}
