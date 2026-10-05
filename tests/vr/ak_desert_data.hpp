#pragma once
#include <array>
#include <string_view>
namespace ak_desert_data
{
// Captured 2026-09-13 from ak47_desert_grenadier, with GP-25 attached.
struct bone {std::string_view name;int parent;};
// Native bind SHA256 b31502424b910ef69c856d39cf39b495f3154e88947a4ac798286c6946c3c90c
inline constexpr std::array<bone,25> receiver{{
	{"j_gun",-1},
	{"j_back_ring",0},
	{"j_bar",0},
	{"j_bolt2",0},
	{"j_front_ring_base",0},
	{"j_gadget",0},
	{"j_gun_trigger",0},
	{"j_trigger",0},
	{"tag_acog_2",0},
	{"tag_brass",0},
	{"tag_clip",0},
	{"tag_clip_02",0},
	{"tag_cover",0},
	{"tag_eotech",0},
	{"tag_flash",0},
	{"tag_gp25",0},
	{"tag_heartbeat",0},
	{"tag_red_dot",0},
	{"tag_shotgun",0},
	{"tag_silencer",0},
	{"tag_thermal_scope",0},
	{"j_bullet01",10},
	{"j_bullet02",10},
	{"j_bullet03",10},
	{"j_front_ring_end",4},
}};
// Native bind SHA256 783b7e57855be89c2561632ef548911403874003328afb57311dfa5892f450ec
inline constexpr std::array<bone,9> gp25{{
	{"tag_gp25",-1},
	{"j_gp25_button",0},
	{"j_gp25_trigger",0},
	{"j_grenade_gp25",0},
	{"j_sight_main",0},
	{"j_grenade_main",3},
	{"j_grenade_shell",3},
	{"j_sight_adjust1",4},
	{"j_sight_adjust2",4},
}};
// Captured from bare ak47_desert on 2026-09-13. The receiver bind digest
// above is identical. This one-bone cover attaches to receiver tag_cover.
// Native bind SHA256 ad427161b6682271f24fc856f28e916c6bf9e7071d1ea818b90dea9f70cf7956
inline constexpr std::array<bone,1> cover{{{"tag_cover",-1}}};
}
