#pragma once
#include "reload_poses.hpp"
#include "../hand_poses/edge_handle.hpp"
#include "../belt_cover_contacts.hpp"
namespace vr::gameplay::weapons::m240
{
inline bool native_family(std::string_view n)noexcept{return native_weapon_family(n,"m240");}
inline const auto handle_grips=hand_poses::edge_handle::at(handle_contact);
// Keep wrist acquisition outside the receiver's right face. The ordinary
// 11 cm contact slack must never reach through the gun onto the cover/belt.
inline constexpr part_capture_halfspace handle_capture{{0,1,0},action_grab_high[1]};
inline const auto feed_interaction=[] {auto p=belt;p.cover_angle=1.570796327f;p.cover_extend_low_m={.12f,.03f,0};p.cover_extend_high_m={.12f,.03f,.12f};p.push=&belt_cover_contacts::m240;return p;}();
inline constexpr mechanics::rules reload_rules{100,mechanics::magazine_release::physical_pull,false,false,false,mechanics::feed_type::open_bolt,false,true};
inline constexpr physical_reload::magazine_manipulation manual_magazine{.06f,.055f,.12f,{0.00000000f, 1.00000000f, 0.00000000f},.025f,.07f,.18f,.012f,{1,0,0},false};
inline const physical_reload::profile interaction{.18f,part_grip_capture::radius_m,action_stroke_m,0,action_stroke_m*.95f,.18f,.065f,.07f,.08715574f,.25f,{-1,0,0},.07f,.35f,.04f,.003f,static_cast<std::uint8_t>(handle_grips.size()),physical_reload::action_motion::charging_handle,&manual_magazine,nullptr,nullptr,&feed_interaction};
inline const char* sound_key(mechanics::effect e)noexcept{switch(e){
case mechanics::effect::magazine_take:return "weap_rpd_clipout_plr";
case mechanics::effect::magazine_in:return "weap_rpd_clipin_plr";
case mechanics::effect::cover_open:return "weap_rpd_open_plr";
case mechanics::effect::cover_close:return "weap_rpd_close_plr";
case mechanics::effect::action_rear:return "weap_rpd_chamber_plr";
case mechanics::effect::belt_laid:return "weap_rpd_hit_plr";
default:return nullptr;}}
inline const reload_profile physical=with_split_sounds([] {reload_profile p{
		.id="m240",
		.native_name="m240",
		.ammunition=reload_rules,
		.interaction=interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_reload",
		.bullets_bone="j_bullet1",
		.magazine_rest=magazine_rest,
		.slide_rest=action_rest,
		.rigid_in_magazine={},
		.magazine_in_wrist=magazine_in_wrist,
		.magazine_top=magazine_top,
		.well=magazine_well,
		.slide_grab_low=action_grab_low,
		.slide_grab_high=action_grab_high,
		.magazine_fingers=magazine_fingers,
		.slide_grips=handle_grips,
		.sound_key=sound_key,
		.native_family=native_family,
		.magazine_contacts=&contacts,
		.rigid_magazine_source="h2_viewmodel_m240_base"
		};p.concealed_bolt=true;p.slide_capture=&handle_capture;return p;}(),"weap_rpd_chamber_plr",false,false);
inline const reload_profile arctic_physical=[] {
 auto p=physical;p.id="m240_arctic";p.rigid_magazine_source="h2_viewmodel_m240_base_arctic";return p;
}();
}
