#pragma once
#include "poses.hpp"
#include "../../weapon_actions.hpp"
#include "../../weapon_profile_binding.hpp"
namespace vr::gameplay::weapons::m79
{
inline bool suppress_equip(std::string_view name)noexcept{return base_equip_action(name,"h2_wpn_lau_m79_");}
inline const profile base=[] {profile p{"m79",feed.receiver,"break_action",aim_rule::two_hand,wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,nullptr,{part_visibility::rigid_groups}};p.break_open=&feed;return p;}();
inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept
{if(receiver.name!=base.receiver || receiver.count!=int(feed.receiver_bones))return {};return bind_profile_attachments(base,models,receiver,rig,bones);}
}
