#pragma once
#include "reload_poses.hpp"
#include "../hand_poses/edge_handle.hpp"
#include "../belt_cover_contacts.hpp"
namespace vr::gameplay::weapons::mg4
{
inline bool native_family(std::string_view n)noexcept{return native_weapon_family(n,"mg4");}
inline const auto handle_grips=hand_poses::edge_handle::at(handle_contact);
inline constexpr mechanics::rules reload_rules{100,mechanics::magazine_release::physical_pull,false,false,false,mechanics::feed_type::open_bolt,false,true};
inline constexpr physical_reload::magazine_manipulation manual_magazine{.06f,.055f,.12f,{0.00000000f, 0.00000000f, 1.00000000f},.025f,.07f,.18f,.012f,{1,0,0},false};
inline const auto feed_interaction=[] {auto p=belt;p.push=&belt_cover_contacts::mg4;return p;}();
inline const physical_reload::profile interaction{.18f,part_grip_capture::radius_m,action_stroke_m,0,action_stroke_m*.95f,.18f,.065f,.07f,.08715574f,.25f,{-1,0,0},.07f,.35f,.04f,.003f,static_cast<std::uint8_t>(handle_grips.size()),physical_reload::action_motion::charging_handle,&manual_magazine,nullptr,nullptr,&feed_interaction};
inline const char* sound_key(mechanics::effect e)noexcept{switch(e){
case mechanics::effect::magazine_take:return "weap_mg4_clipout_plr";
case mechanics::effect::magazine_in:return "weap_mg4_clipin_plr";
case mechanics::effect::cover_open:return "weap_mg4_open_plr";
case mechanics::effect::cover_close:return "weap_mg4_close_plr";
case mechanics::effect::action_rear:return "weap_mg4_chamber_plr";
case mechanics::effect::belt_laid:return "weap_mg4_hit_plr";
default:return nullptr;}}
inline const reload_profile physical=with_split_sounds([] {reload_profile p{
		.id="mg4",
		.native_name="mg4",
		.ammunition=reload_rules,
		.interaction=interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_bolt",
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
		.rigid_magazine_source="h2_viewmodel_mg4_base"
		};p.concealed_bolt=true;return p;}(),"weap_mg4_chamber_plr",false,false);
inline const reload_profile arctic_physical=[] {
 auto p=physical;p.id="mg4_arctic";p.rigid_magazine_source="h2_viewmodel_mg4_base_arctic";return p;
}();
}
