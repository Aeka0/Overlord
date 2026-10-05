#pragma once
#include <array>
#include <string_view>
namespace acr_arctic_data
{
// Captured 2026-09-13 from masada_silencer_mt_camo_on_h2.
struct bone {std::string_view name;int parent;};
// Native bind SHA256 792b2be20eccfea66aaf0eca92759e96bd87327e1ad7e8da03cd388a33d3e6fc
inline constexpr std::array<bone,19> receiver{{
	{"j_gun",-1},
	{"j_bolt",0},
	{"j_trigger",0},
	{"tag_acog_2",0},
	{"tag_brass",0},
	{"tag_clip",0},
	{"tag_eotech",0},
	{"tag_flash",0},
	{"tag_front_sight_off",0},
	{"tag_front_sight_on",0},
	{"tag_heartbeat",0},
	{"tag_m203",0},
	{"tag_rear_sight",0},
	{"tag_red_dot",0},
	{"tag_shotgun",0},
	{"tag_silencer",0},
	{"tag_thermal_scope",0},
	{"j_bullets",5},
	{"tag_bullet",5},
}};
// Native bind SHA256 cc44c949605442d1b456b6dff220f66a11d48b949a12a76b62ce78b2fd492bee
inline constexpr std::array<bone,9> sensor{{
	{"tag_heartbeat",-1},
	{"j_motion_tracker_roty",0},
	{"j_motion_tracker_rotz",1},
	{"tag_motion_tracker",2},
	{"j_wire_base",3},
	{"tag_screen_bl",3},
	{"tag_screen_br",3},
	{"tag_screen_tl",3},
	{"tag_screen_tr",3},
}};
}
