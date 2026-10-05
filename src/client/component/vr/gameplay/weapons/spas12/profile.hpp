#pragma once
#include "poses.hpp"
#include "reload_poses.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"
namespace vr::gameplay::weapons::spas12
{
inline constexpr std::array<std::string_view,3> names{"spas12","spas12_reflex","spas12_eotech"};
inline constexpr std::array<std::string_view,2> arctic_names{"spas12_arctic","spas12_arctic_reflex"};
inline sound_reference sound(tube::effect e)noexcept{switch(e){
case tube::effect::draw:return {"weap_spas12_lift_plr"};
case tube::effect::load_tube:case tube::effect::load_port:return {"weap_spas12_loop_plr"};
case tube::effect::rack_open:return {"weap_spas12_open_plr"};
case tube::effect::rack_close:return {"weap_spas12_close_plr"};
default:return {};}}
inline constexpr tube::tuning interaction{{.18f,part_grip_capture::radius_m,0.07340460f,0.06740460f,0.07040460f,.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,1},.035f,.035f,.35f,{.015f,.04f}};
inline const tube_profile feed{"spas12","h2_viewmodel_spas12_base",names,{7,tube::action_drive::pump},interaction,bolt_rest,lifter_rest,lifter_loaded,shell_in_wrist,shell_center,port_center,tube_center,port_forward,tube_forward,rack_low,rack_high,{&rack_pose,1},shell_fingers,sound,"j_reload","j_reload_plate","tag_clip",17,"j_pump",pump_rest,bolt_open,port_shell};
// Oilrig live assets: the arctic receiver's 17 bones, parents and all bind
// transforms exactly match the base. Keep separate model/native admission.
inline const tube_profile arctic_feed=[] {auto p=feed;p.receiver="h2_viewmodel_spas12_base_arctic";p.native_variants=arctic_names;return p;}();
inline bool suppress_equip(std::string_view n)noexcept{return base_equip_action(n,"h2_wpn_sho_spas12_");}
inline const profile base=[] {profile p{"spas12","h2_viewmodel_spas12_base","pump",aim_rule::two_hand,wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,nullptr,{part_visibility::rigid_groups}};p.tube=&feed;return p;}();
inline const profile arctic=[] {auto p=base;p.receiver=arctic_feed.receiver;p.tube=&arctic_feed;return p;}();
inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept{
const auto* p=receiver.name==base.receiver ? &base : receiver.name==arctic.receiver ? &arctic : nullptr;
if(!p || receiver.count!=17)return {nullptr,"pump receiver topology rejected"};const auto b=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);if(!b.valid)return {nullptr,"pump attachment topology rejected"};return {p,"manual pump tube feed",{},b.muzzle};}
}
