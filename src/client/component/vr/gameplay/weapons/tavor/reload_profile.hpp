#pragma once
#include "../../magazine_grasp_profile.hpp"
#include "reload_poses.hpp"
#include "magazine_grasps.hpp"

namespace vr::gameplay::weapons::tavor
{
inline bool native_family(std::string_view name) noexcept { return native_weapon_family(name,"tavor"); }
inline constexpr mechanics::rules reload_rules{30,mechanics::magazine_release::physical_pull,true,true,true};
// Rear button takes forward/upward strikes; the separate forward paddle takes
// rearward strikes (contacts.second_latch). Deliberate extraction is unchanged.
inline constexpr physical_reload::magazine_manipulation manual_magazine{.05f,.05f,.10f,{0,0,-1},.025f,.06f,.15f,.012f,{.70710678f,0,.70710678f},true};
// Exported j_reload does not reciprocate in fire; it returns independently.
inline constexpr physical_reload::profile reload_interaction{.18f,part_grip_capture::radius_m,action_stroke_m,0,action_stroke_m*.95f,.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,2,physical_reload::action_motion::charging_handle,&manual_magazine};
inline const char* sound_key(mechanics::effect kind) noexcept
{
using enum mechanics::effect;
switch(kind) {
case magazine_out: case magazine_take: return "weap_tavor_clipout_plr";
case magazine_in: return "weap_tavor_clipin_plr";
case action_close: return "weap_tavor_chamber_plr";
default: return nullptr;
}
}
inline constexpr std::array<std::string_view,1> magazine_structure{"j_plate"};
inline const reload_profile physical=with_split_sounds([] {
reload_profile p{
		.id="tavor",
		.native_name="tavor",
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_reload",
		.bullets_bone="j_bullet",
		.magazine_rest=magazine_rest,
		.slide_rest=action_rest,
		.rigid_in_magazine={},
		.magazine_in_wrist=magazine_in_wrist,
		.magazine_top=magazine_top,
		.well=magazine_well,
		.slide_grab_low=action_grab_low,
		.slide_grab_high=action_grab_high,
		.magazine_fingers=magazine_fingers,
		.slide_grips=action_grips,
		.sound_key=sound_key,
		.native_family=native_family,
		.magazine_contacts=&contacts,
		.rigid_magazine_source="h2_viewmodel_tavor_base"
		};
p.magazine_body_bones=magazine_structure;
p.magazine_grasps=magazine_grasps;
p.interaction=physical_reload::with_box_magazine_well(p.interaction);
return with_controller_magazine(p);
}(),"weap_tavor_chamber_plr",false,false);
inline const reload_profile digital=[] {auto p=physical;p.id="tavor_digital";p.rigid_magazine_source="h2_viewmodel_tavor_base_digital";return p;}();
inline const reload_profile woodland=[] {auto p=physical;p.id="tavor_woodland";p.rigid_magazine_source="h2_viewmodel_tavor_base_woodland";return p;}();
inline const std::array<const reload_profile*,3> skins{&physical,&digital,&woodland};
static_assert(action_grips.size()==reload_interaction.slide_pose_count);
}
