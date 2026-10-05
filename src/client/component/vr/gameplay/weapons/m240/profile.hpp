#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"
namespace vr::gameplay::weapons::m240 {
inline bool suppress_equip(std::string_view n)noexcept{return base_equip_action(n,"h2_wpn_lmg_m240_");}
inline const profile base{"m240","h2_viewmodel_m240_base","support",aim_rule::two_hand,wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
// Live heartbeat/reflex arctic variant shares the reviewed 38-bone receiver.
// Keep its own source model for detached boxes and native camouflage.
inline const profile arctic=[] {
 auto p=base;p.receiver=arctic_physical.rigid_magazine_source;p.reload=&arctic_physical;return p;
}();
inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,const hands::rig& r,std::span<const hands::bone_definition> bones)noexcept {
 const auto* selected=receiver.name==base.receiver ? &base : receiver.name==arctic.receiver ? &arctic : nullptr;
 if(!selected || receiver.count!=38)return {};
 const auto bound=bind_attachment_set(rifle_attachments::common,models,receiver,r,bones);
 if(!bound.valid)return {nullptr,"Belt weapon attachment topology rejected"};
 return {selected,"Open-bolt box and belt feed",{},bound.muzzle};
}
}
