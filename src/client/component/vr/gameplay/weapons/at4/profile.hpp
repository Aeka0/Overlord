#pragma once
#include "poses.hpp"
#include "../../weapon_actions.hpp"
#include "../../weapon_profile_binding.hpp"
namespace vr::gameplay::weapons::at4
{
inline bool suppress_equip(std::string_view name)noexcept{return base_equip_action(name,"h2_wpn_lau_at4_");}
// Broad tube support capture, with a wider release threshold to avoid chatter.
inline const profile base=[] {profile out{"at4","h2_viewmodel_at4_base","launcher",aim_rule::two_hand,wrists,1,.24f,.34f,.10f,idle_fingers,equip_rest,suppress_equip,nullptr,{part_visibility::rigid_groups}};out.launcher=&feed;return out;}();
inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept
{
if(receiver.name!=base.receiver || receiver.count!=10)return {nullptr,"launcher receiver contract rejected"};
return bind_profile_attachments(base,models,receiver,rig,bones);
}
}
