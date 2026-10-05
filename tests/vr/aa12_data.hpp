#pragma once
#include <array>
#include <string_view>
namespace aa12_data
{
struct bone {std::string_view name;int parent;};
// h2_viewmodel_aa12_base SHA-256 e7505533790245f78285348e5726fd86d56eee2c6fbac7374aa7f7574191a8d2
inline constexpr std::array<bone,14> receiver{{
{"j_gun",-1},
{"j_reload",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_eotech",0},
{"tag_flash",0},
{"tag_foregrip",0},
{"tag_heartbeat",0},
{"tag_rail",0},
{"tag_red_dot",0},
{"tag_sight_on",0},
{"tag_silencer",0},
{"j_bullet",3},
{"j_reload_end",1},
}};
}
