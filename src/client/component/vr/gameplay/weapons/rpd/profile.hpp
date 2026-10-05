#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"
namespace vr::gameplay::weapons::rpd {
inline constexpr auto attachments=rifle_attachments::with_common(std::array<assembly_attachment,3>{{
 {{"attach_h2_rpd_foregrip_default_vm","tag_foregrip","tag_foregrip"},attachment_role::foregrip,1},
 {{"attach_h2_rpd_bipod_vm","tag_bipods","tag_bipods"},attachment_role::bipod,4},
 {{"attach_h2_rpd_foregrip_default_vm_digital","tag_foregrip","tag_foregrip"},attachment_role::foregrip,1}
}});
inline bool suppress_equip(std::string_view n)noexcept{return base_equip_action(n,"h2_wpn_lmg_rpd_");}
inline const profile base{"rpd","h2_viewmodel_rpd_base","support",aim_rule::two_hand,wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,&physical,{part_visibility::rigid_groups}};
// Live dcemp assets have identical base/digital skeletons and bind matrices.
// Keep a distinct source recipe so detached drums retain their native material.
inline const profile digital=[] {
 auto p=base;p.receiver=digital_physical.rigid_magazine_source;p.reload=&digital_physical;return p;
}();
inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,const hands::rig& r,std::span<const hands::bone_definition> bones)noexcept {
 const auto* selected=receiver.name==base.receiver ? &base : receiver.name==digital.receiver ? &digital : nullptr;
 if(!selected || receiver.count!=51)return {};
 const auto bound=bind_attachment_set(attachments,models,receiver,r,bones);
 if(!bound.valid)return {nullptr,"RPD attachment topology rejected"};
 return {selected,"Open-bolt drum, belt and optic bridge",{},bound.muzzle};
}
}
