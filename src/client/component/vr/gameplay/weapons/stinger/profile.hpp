#pragma once
#include "poses.hpp"
#include "../../weapon_actions.hpp"
#include "../../weapon_profile_binding.hpp"
namespace vr::gameplay::weapons::stinger
{
inline bool suppress_equip(std::string_view name)noexcept{return base_equip_action(name,"h2_wpn_lau_stinger_");}
inline const profile base=[] {profile out{"stinger","h2_viewmodel_stinger","launcher",aim_rule::two_hand,wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,nullptr,{part_visibility::rigid_groups}};out.launcher=&feed;return out;}();
inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept
{
if(receiver.name!=base.receiver || receiver.count!=5)return {nullptr,"launcher receiver contract rejected"};
return bind_profile_attachments(base,models,receiver,rig,bones);
}
}
