#pragma once
#include <array>
#include <string_view>
namespace tavor_fn2000_data
{
struct bone {std::string_view name;int parent;};
// h2_viewmodel_tavor_base SHA-256 0c1603120beb517c31ec33de82a6d6315ae827963c16ad276151acc0c6e604a2
inline constexpr std::array<bone,17> tavor{{
{"j_gun",-1},
{"j_reload",0},
{"tag_acog_2",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_eotech",0},
{"tag_flash",0},
{"tag_heartbeat",0},
{"tag_m203",0},
{"tag_red_dot",0},
{"tag_shotgun",0},
{"tag_sight_on",0},
{"tag_silencer",0},
{"tag_tavor_scope",0},
{"tag_thermal",0},
{"j_bullet",4},
{"j_plate",4},
}};
// h2_viewmodel_tavor_base_digital SHA-256 f25a2f08612a305aa9a640e2b309d9724fdf960ae86ae8542362bd6c32c1cd54
inline constexpr std::array<bone,17> digital{{
{"j_gun",-1},
{"j_reload",0},
{"tag_acog_2",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_eotech",0},
{"tag_flash",0},
{"tag_heartbeat",0},
{"tag_m203",0},
{"tag_red_dot",0},
{"tag_shotgun",0},
{"tag_sight_on",0},
{"tag_silencer",0},
{"tag_tavor_scope",0},
{"tag_thermal",0},
{"j_bullet",4},
{"j_plate",4},
}};
// h2_viewmodel_fn2000_base SHA-256 35af0bd35ce62c5f23747d7e3da55b251c7840371f34beed8b6777978ce0d999
inline constexpr std::array<bone,21> fn2000{{
{"j_gun",-1},
{"j_flap",0},
{"j_reload",0},
{"j_trigger",0},
{"tag_acog_2",0},
{"tag_body_bottom",0},
{"tag_brass",0},
{"tag_clip",0},
{"tag_eotech",0},
{"tag_flash",0},
{"tag_fn2000_scope",0},
{"tag_heartbeat",0},
{"tag_m203",0},
{"tag_rail",0},
{"tag_red_dot",0},
{"tag_shotgun",0},
{"tag_sight_off",0},
{"tag_sight_on",0},
{"tag_silencer",0},
{"tag_thermal_scope",0},
{"j_bullets",7},
}};
// attach_h2_tavor_scope_vm SHA-256 633487e5daef8aa71daa0d6b776eb41cef2e7a4faf143c3fe71429eb3f088881
inline constexpr std::array<bone,2> mars{{
{"tag_tavor_scope",-1},
{"tag_reticle_tavor_scope",0},
}};
}
