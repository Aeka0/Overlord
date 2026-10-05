#pragma once
#include "poses.hpp"
#include "reload_poses.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"
namespace vr::gameplay::weapons::winchester1200
{
inline constexpr std::array<std::string_view,1> names{"winchester1200"};
inline sound_reference sound(tube::effect e)noexcept{switch(e){
case tube::effect::draw:return {"h2_wpn_w1200_lift_plr"};
case tube::effect::load_tube:case tube::effect::load_port:return {"weap_winch1200_loop_plr"};
case tube::effect::rack_open:return {"h2_wpn_w1200_open_plr"};
case tube::effect::rack_close:return {"h2_wpn_w1200_close_plr"};
default:return {};}}
inline constexpr tube::tuning interaction{{.18f,part_grip_capture::radius_m,0.07659368f,0.07059368f,0.07359368f,.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,1},.035f,.035f,.35f,{.015f,.04f}};
inline const tube_profile feed{"winchester1200","h2_viewmodel_winchester1200_base",names,{7,tube::action_drive::pump},interaction,bolt_rest,lifter_rest,lifter_loaded,shell_in_wrist,shell_center,port_center,tube_center,port_forward,tube_forward,rack_low,rack_high,{&rack_pose,1},shell_fingers,sound,"j_slide","j_load","tag_clip",16,"j_pump",pump_rest,bolt_open,port_shell};
inline bool suppress_equip(std::string_view n)noexcept{return base_equip_action(n,"h2_wpn_sho_w1200_");}
inline const profile base=[] {profile p{"winchester1200","h2_viewmodel_winchester1200_base","pump",aim_rule::two_hand,wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,nullptr,{part_visibility::rigid_groups}};p.tube=&feed;return p;}();
inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept{
if(receiver.count!=16)return {nullptr,"pump receiver topology rejected"};const auto b=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);if(!b.valid)return {nullptr,"pump attachment topology rejected"};return {&base,"manual pump tube feed",{},b.muzzle};}
}
