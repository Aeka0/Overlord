#pragma once
#include "component/vr/gameplay/hand_rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "marine_sniper_hand_data.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
namespace marine_sniper_hand_tests
{
 template<class Check> void run(Check check)
 {
  using namespace vr::gameplay;using namespace hands;
  auto profile=weapons::m14ebr::assemblies[0];profile.equip_rest={};
  const model_definition model{"viewhands_marine_sniper",0,67};
  const auto rig=resolve_rig({&model,1},marine_sniper_data::bones,rig_kind::hands_only);
  check(!rig.rejection,"captured marine sniper hand has a valid native arm skeleton");
  const auto bound=bind_weapon_poses(rig.layout,marine_sniper_data::bones,profile);
  check(bound.valid,"M14 pose admits real boneyard hand without skin-webbing leaves");
  unsigned fingers{};for(int i=0;i<rig.layout.count;++i)if(bound.finger[i]>=0)++fingers;
  check(fingers==34,"both complete articulated hands remain bound; only two absent deformation leaves are skipped");
  auto missing=marine_sniper_data::bones;
  for(auto& b:missing)if(b.name=="j_index_ri_0")b.name="missing_required_finger";
  check(!bind_weapon_poses(rig.layout,missing,profile).valid,"missing real finger joint remains a hard rejection");
  missing=marine_sniper_data::bones;
  for(auto& b:missing)if(b.name=="j_ringpalm_le")b.parent=0;
  auto changed=rig.layout;for(int i=0;i<changed.count;++i)changed.parent[i]=missing[i].parent;
  check(!bind_weapon_poses(changed,missing,profile).valid,"detached palm cannot masquerade as optional skin geometry");
  std::array<bone_definition,69> full{};std::copy(marine_sniper_data::bones.begin(),marine_sniper_data::bones.end(),full.begin());
  for(unsigned h=0;h<2;++h){const int wrist=rig.layout.arms[h].wrist;full[67+h]={h?"j_webbing_ri":"j_webbing_le",wrist,full[wrist].bind};}
  const model_definition expanded{"test_hand_with_webbing",0,69};const auto expanded_rig=resolve_rig({&expanded,1},full,rig_kind::hands_only);
  check(!expanded_rig.rejection && bind_weapon_poses(expanded_rig.layout,full,profile).valid,"gloves with webbing still bind and animate those nodes");
  full[67].bind.rotation={0,0,0,0};
  check(!bind_weapon_poses(expanded_rig.layout,full,profile).valid,"present optional leaf with corrupt transform remains rejected");
  check(!optional_hand_joint("j_ring_le_0") && !optional_hand_joint("j_ringpalm_le") && !optional_hand_joint("j_webbing_extra"),"optional rule is limited to the two witnessed leaf names");
 }
}
