#pragma once
#include "component/vr/gameplay/hand_rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/riot_shield/profile.hpp"
#include "riot_shield_data.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_carry_pose.hpp"
#include "component/vr/gameplay/weapon_clip_projection.hpp"
#include "component/vr/gameplay/shield_trace.hpp"
#include "component/vr/gameplay/grip_presenter.hpp"
#include "component/vr/gameplay/empty_hand_pose.hpp"
#include <limits>
namespace shield_tests
{
 template<class Check> void run(Check check)
 {
  using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
  check(shield::ballistic_damage(1) && shield::ballistic_damage(2) && !shield::ballistic_damage(3) &&
   !shield::ballistic_damage(8) && !shield::ballistic_damage(-1),"only ordinary bullet damage enters shield policy; explosive and melee damage remain native");
  const auto& profile=riot_shield::base;
  auto bones=riot_shield_data::bones;auto models=riot_shield_data::models;
  const auto rejected=resolve_rig(models,bones);
  check(rejected.rejection!=nullptr,"shield must not relax ordinary firearm axis validation");
  const auto resolved=resolve_rig(models,bones,rig_kind::shield);const auto& rig=resolved.layout;
  check(!resolved.rejection,"reviewed shield has an explicit non-firing rig contract");
  check(select_profile(models,rig,bones).value==&profile,"real shield skeleton selects registered defense profile");
  auto malformed=bones;malformed.back().parent=0;
  check(!select_profile(models,rig,malformed).value,"changed strap ownership rejects shield profile");
  models[1].name="other_shield";
  check(resolve_rig(models,bones,rig_kind::shield).rejection!=nullptr,"unknown shield cannot opt into axis exception");
  check(profile.defense && !profile.reload && !profile.tube && !profile.cylinder && !profile.break_open &&
   !profile.support_enabled,"shield has defense and no ammunition or second-hand acquisition");
  const auto library=bind_weapon_poses(rig,bones,profile);check(library.valid,"native shield fingers bind through common hand library");
  hold owner{7,1,hand::left,hand::none};owner.instance_generation=5;
  const carry::pose_profile mirrored(profile,owner,rig,library);
  check(length(sub(mirrored.controls[0].position,mirrored.controls[1].position))<.0001f,"explicit bilateral grips retain the same physical shield handle");
  check(length(sub(rotate(mirrored.controls[0].rotation,{1,0,0}),rotate(profile.wrists[0].rotation,{1,0,0})))<.001f &&
   length(sub(rotate(mirrored.controls[0].rotation,{0,1,0}),rotate(profile.wrists[0].rotation,{0,1,0})))<.001f,"native left-hand basis is used directly without mirror roundtrip");
  for(const auto side:{hand::left,hand::right})
  {
   auto held=owner;held.rear=side;const int h=int(side);
   carry::pose_profile fit(profile,held,rig,library);
   check(fit.value.authored_rear==h && fit.value.fingers.data()==profile.fingers.data(),"bilateral shield never remirrors already authored fingers");
   std::array<bone,riot_shield_data::bones.size()> native{},solved{};
   for(std::size_t i=0;i<native.size();++i)native[i]=bones[i].bind;
   std::array<anchor,2> targets{{{{20,9,0},{0,0,0,1}},{{20,-9,0},{0,0,0,1}}}};
   const std::array<vec,2> shoulders{vec{0,7,8},vec{0,-7,8}};
   const std::array<vec,3> axes{vec{1,0,0},vec{0,1,0},vec{0,0,1}};
   std::array<bool,2> limited{};vr::controller_input::frame input;
   const auto now=vr::controller_input::clock::now();input.sequence=1;input.reference_generation=1;input.sampled_at=now;input.focused=true;
   grip_presenter presenter;
   const auto result=presenter.update(fit.value,library,rig,native,targets,shoulders,axes,input,fit.solver_owner,1,39.37007874f,true,true,now,solved,limited,false,true);
   check(result.valid,"both shield controlling hands complete the full common IK and finger solve");
   const auto gun=vr::gameplay::hands::pose_math::as_anchor(solved[rig.gun]);
   check(length(sub(vr::gameplay::hands::pose_math::compose(gun,fit.controls[h]).position,solved[rig.arms[h].wrist].position))<.001f,"visible handle and solved wrist remain coincident after model roll");
   const auto arm=rig.arms[h];const auto forearm=unit(sub(solved[arm.elbow].position,solved[arm.wrist].position));
   check(length(sub(rotate(gun.rotation,profile.forearm->axis),forearm))<.001f,"shield cuff axis is fixed to the actual solved forearm in either hand");
   const auto cuff=vr::gameplay::hands::pose_math::compose(gun,{add(fit.controls[h].position,scale(profile.forearm->axis,7.5f)),{0,0,0,1}}).position;
   check(length(cross(sub(cuff,solved[arm.wrist].position),forearm))<.001f &&
    dot(sub(cuff,solved[arm.wrist].position),forearm)<length(sub(solved[arm.elbow].position,solved[arm.wrist].position)),"forearm segment passes through cuff between wrist and elbow");
   check(std::abs(length(sub(solved[arm.elbow].position,shoulders[h]))-length(sub(native[arm.elbow].position,native[arm.shoulder].position)))<.001f &&
    std::abs(length(sub(solved[arm.wrist].position,solved[arm.elbow].position))-length(sub(native[arm.wrist].position,native[arm.elbow].position)))<.001f,"mount preserves shoulder position and both bone lengths at ordinary reach");
   check(length(sub(rotate(solved[arm.wrist].rotation,{0,1,0}),rotate(multiply(gun.rotation,fit.controls[h].rotation),{0,1,0})))<.001f,"hand keeps its exact authored grip while shield seats on the forearm");
   const auto mounted=mount_forearm(*profile.forearm,length(sub(native[arm.elbow].position,native[arm.shoulder].position)),
    length(sub(native[arm.wrist].position,native[arm.elbow].position)),{targets[h].position,control_rotation(profile,h,targets[h].rotation)},shoulders[h],axes,h);
   check(mounted.limb.valid && length(sub(rotate(mounted.rotation,{1,0,0}),rotate(gun.rotation,{1,0,0})))<.001f,"server carry and renderer use the same forearm mounting solution");
   auto fallback=native;const auto original_fallback=fallback;
   check(apply_mounted_limb(rig,fallback,h,mounted,shoulders[h],normalize(multiply(mounted.rotation,fit.controls[h].rotation))),"other held hand can use the same mount in a shared-arm viewmodel");
   for(int i=0;i<rig.count;++i)if(rig.weapon_bones[i] || descendant(i,rig.arms[1-h].shoulder,rig))
    check(fallback[i].position==original_fallback[i].position && fallback[i].rotation==original_fallback[i].rotation,"forearm mount does not disturb the other weapon or arm");
   for(int i=0;i<rig.count;++i)if(library.finger[i]>=0 && descendant(i,rig.arms[h].wrist,rig))
   {
    const auto actual=normalize(multiply(conjugate(solved[rig.parent[i]].rotation),solved[i].rotation));
    const auto expected=profile.fingers[library.finger[i]].rotation;
    check(length(sub(rotate(actual,{1,0,0}),rotate(expected,{1,0,0})))<.001f &&
     length(sub(rotate(actual,{0,1,0}),rotate(expected,{0,1,0})))<.001f,"shield finger hierarchy retains every authored joint, including ring palm and ring finger");
   }
   const int free=1-h;const auto neutral=normalize(multiply(conjugate(normalize(bones[rig.weapon_tag].bind.rotation)),normalize(bones[rig.arms[free].wrist].bind.rotation)));
   const auto held_before=solved[rig.arms[h].wrist];
   check(empty_hand::orient_wrist(rig,hand(free),{},false,targets[free].rotation,neutral,solved),"empty opposite hand uses its own neutral wrist service");
   check(length(sub(rotate(solved[rig.arms[free].wrist].rotation,{1,0,0}),rotate(multiply(targets[free].rotation,neutral),{1,0,0})))<.001f,
    "empty wrist uses hand model basis instead of shield grip basis");
   check(solved[rig.gun].rotation==gun.rotation && solved[rig.arms[h].wrist].rotation==held_before.rotation,
    "free wrist orientation cannot change mounted shield or its holding hand");
   const auto arm_geometry=capture_arm(rig,solved,h,{100,20,-30},40);
   check(arm_geometry.valid && arm_geometry.joints[1]==add(solved[rig.arms[h].elbow].position,vec{100,20,-30}) &&
    arm_geometry.joints[2]==add(solved[rig.arms[h].wrist].position,vec{100,20,-30}),"firearm anti-penetration captures real solved elbow and wrist with the same world origin");
   for(const auto target:{vec{15,0,5},vec{7,12,18},vec{5,-10,-6},vec{40,5,3},vec{0,7,8}})
    for(const auto rotation:{quat{0,0,0,1},normalize(quat{.2f,.7f,.3f,.6f}),quat{1,0,0,0}})
    {
     const auto m=mount_forearm(*profile.forearm,12,11,{target,control_rotation(profile,h,rotation)},shoulders[h],axes,h);
     check(m.limb.valid && length(sub(rotate(m.rotation,profile.forearm->axis),unit(sub(m.limb.elbow,m.limb.wrist))))<.001f,
      "raised, crossed, extended and folded arms keep a finite cuff-aligned pose");
     check(length(sub(m.limb.elbow,shoulders[h]))<=12*max_arm_stretch_ratio+.001f &&
      length(sub(m.limb.wrist,m.limb.elbow))<=11*max_arm_stretch_ratio+.001f,"extreme mount inputs retain existing stretch limits");
    }
  }
  const auto& defense=*profile.defense;
  const anchor identity{{0,0,0},{0,0,0,1}};
  const auto hit=shield::intersect(defense,identity,{100,0,0},{-100,0,0});
  check(hit.valid && hit.fraction<.5f && hit.normal[0]>.9f,"front body panel stops an incoming segment");
  check(!shield::intersect(defense,identity,{-100,0,0},{100,0,0}).valid,"rear-side fire does not gain frontal protection");
  check(shield::intersect(defense,identity,{-100,0,0},{100,0,0},1,shield::faces::both).valid,"outgoing pellets hit the back face while incoming front-only policy stays unchanged");
  check(!shield::intersect(defense,identity,{100,30,0},{-100,30,0}).valid,"front-facing shield does not cover exposed body beside it");
  check(shield::intersect(defense,identity,{100,0,14},{-100,0,14}).valid,"transparent observation window remains ballistic protection");
  check(!shield::intersect(defense,identity,{100,11.3f,19},{-100,11.3f,19}).valid,"rounded corners do not create a rectangular invisible extension");
  check(!shield::intersect(defense,identity,{100,0,0},{-100,0,0},hit.fraction*.5f).valid,"earlier wall or body impact wins");
  check(!shield::intersect(defense,identity,{100,0,0},{50,0,0}).valid,"finite segment ending before shield is not blocked");
  check(!shield::intersect(defense,identity,{100,0,0},{100,10,0}).valid,"parallel segment is not a shield hit");
  check(!shield::intersect(defense,identity,{0,0,0},{0,0,0}).valid,"zero-length bullet segment rejected");
  check(shield::barrel_penetrates(defense,identity,{-10,0,0},{10,0,0}),"muzzle protruding through shield is blocked by hand-to-muzzle span");
  check(!shield::barrel_penetrates(defense,identity,{-15,0,0},{-10,0,0}),"gun entirely behind shield may fire before its bullet hits the shield");
  const auto back_hit=shield::intersect(defense,identity,{-10,0,0},{100,0,0},1,shield::faces::both);
  check(back_hit.valid && back_hit.normal[0]<0,"a shot from behind emits impact effects facing the back of the shield");
  check(!shield::barrel_penetrates(defense,identity,{-15,0,14},{-10,0,14}),"shooting the glass from behind is a normal shot, not barrel penetration");
  check(!shield::barrel_penetrates(defense,identity,{-15,20,0},{-10,20,0}),"gun beyond shield side can fire around it");
  check(!shield::barrel_penetrates(defense,identity,{-15,0,30},{-10,0,30}),"gun above shield can fire over it");
  check(!shield::barrel_penetrates(defense,identity,{10,0,0},{15,0,0}),"gun entirely in front has no barrel penetration");
  check(shield::barrel_penetrates(defense,identity,{-10,0,0},hit.point),"muzzle exactly on the shield surface cannot slip through endpoint rounding");
  const arm_geometry behind{{vec{-25,0,0},vec{-18,0,0},vec{-10,0,0}},40,true};
  check(shield::fire_penetration(defense,identity,behind,{-5,0,0})==shield::clipping::none &&
   shield::intersect(defense,identity,{-5,0,0},{100,0,0},1,shield::faces::both).valid,
   "unclipped arm and barrel behind shield are permitted; the emitted bullet hits the back face");
  const arm_geometry forearm_cheat{{vec{-25,0,0},vec{-10,0,0},vec{10,0,0}},40,true};
  check(shield::fire_penetration(defense,identity,forearm_cheat,{15,0,0})==shield::clipping::forearm,
   "gun entirely ahead of shield cannot fire when the forearm passed through it");
  const arm_geometry upper_cheat{{vec{-10,0,0},vec{10,0,0},vec{18,0,0}},40,true};
  check(shield::fire_penetration(defense,identity,upper_cheat,{23,0,0})==shield::clipping::upper_arm,
   "moving elbow and wrist through shield still catches the upper arm");
  const arm_geometry around_edge{{vec{-20,20,0},vec{10,20,0},vec{18,20,0}},40,true};
  check(shield::fire_penetration(defense,identity,around_edge,{23,20,0})==shield::clipping::none,"arm around the actual finite shield edge remains usable");
  const arm_geometry grazing{{vec{-20,12,0},vec{-10,12,0},vec{10,12,0}},40,true};
  check(shield::fire_penetration(defense,identity,grazing,{20,12,0})==shield::clipping::forearm,"arm thickness catches grazing penetration even if its centerline misses");
  check(shield::fire_penetration(defense,identity,{}, {20,0,0})==shield::clipping::invalid,"missing arm publication cannot grant firing through a held shield");
  const shield::triangle panel{{0,-2,-2},{0,2,-2},{0,0,2}};
  check(shield::segment_panel_distance_squared({0,-1,0},{0,1,0},panel)<1e-6f,"coplanar arm contact is not missed by a parallel ray test");
  check(std::abs(shield::segment_panel_distance_squared({3,-1,0},{3,1,0},panel)-9)<1e-5f,"parallel separated limb has correct surface clearance");
  check(shield::segment_distance_squared({0,0,0},{0,0,0},{2,0,0},{2,0,0})==4,"collapsed segments remain finite");
  const float q=std::sqrt(.5f);
  for(const auto rotation:{quat{0,0,q,q},quat{0,q,0,q},quat{q,0,0,q},normalize(quat{.3f,.5f,.1f,.7f})})
  {
   const anchor world{{125,-43,67},rotation};
   const auto point=[&](vec p){return add(world.position,rotate(rotation,p));};
   const auto transformed=shield::intersect(defense,world,point({100,0,0}),point({-100,0,0}));
   check(transformed.valid && std::abs(transformed.fraction-hit.fraction)<.00001f &&
    length(sub(transformed.point,point(hit.point)))<.001f,"shield rotation and translation preserve actual intersection");
   auto arm=forearm_cheat;for(auto& joint:arm.joints)joint=point(joint);
   check(shield::fire_penetration(defense,world,arm,point({15,0,0}))==shield::clipping::forearm,"arm penetration follows translated, tilted and rolled shields");
   check(!shield::barrel_penetrates(defense,world,point({-15,0,0}),point({-10,0,0})),"legitimate firing from behind stays allowed after shield rotation");
  }
  auto invalid=identity;invalid.rotation={0,0,0,0};
  check(!shield::intersect(defense,invalid,{100,0,0},{-100,0,0}).valid,"invalid pose cannot create protection");
  check(!shield::intersect(defense,identity,{NAN,0,0},{-100,0,0}).valid,"nonfinite bullet rejected");
  clip_ledger ledger;const clip_ledger::definition inventory[]{{7,0,0,2},{8,12,80,3}};
  check(ledger.reconcile(inventory),"ammunitionless shield and firearm share instance lifecycle");
  const auto shield_id=ledger.projected(7),gun_id=ledger.projected(8);
  check(ledger.reconcile(inventory) && ledger.projected(7)==shield_id,"reconciliation retains shield identity without ammo records");
  check(!ledger.set(shield_id,0,1) && !ledger.add(ledger.allocate(7),0,0,2),"shield cannot receive ammunition or duplicate same-definition instances");
  std::array<std::byte,native_ammunition::storage::extent> memory{};const auto before=memory;
  check(!native_ammunition::projection::select(ledger,memory,shield_id,0,0) && memory==before,"shield cannot project into native ammunition storage");
  check(ledger.erase(shield_id) && ledger.find(gun_id)->loaded==12,"removing shield preserves other gun ammunition");
  check(ledger.reconcile(inventory) && ledger.projected(7)!=shield_id,"new shield ownership gets a new lifetime");
  muzzle_frame frame;frame.valid=true;frame.owner=owner;frame.input_sequence=1;frame.reference_generation=1;
  frame.sampled_at=frame.camera_at=vr::controller_input::clock::now();frame.axis={vec{1,0,0},vec{0,1,0},vec{0,0,1}};frame.firing_capable=false;
  check(pose_ready(frame,owner,1,frame.sampled_at) && !ready(frame,owner,1,frame.sampled_at),"shield skin frame remains usable but cannot authorize a shot");
  vr::controller_input::frame input;input.sequence=4;input.reference_generation=3;input.focused=true;input.sampled_at=frame.sampled_at;
  input.grip[0].valid=input.aim[0].valid=true;
  const auto fresh=[&](const hold& pose_owner,auto at){return shield::pose_usable(pose_owner,owner,4,3,at,input,frame.sampled_at);};
  check(fresh(owner,frame.sampled_at),"current held shield admits same-reference pose");
  check(!fresh(owner,frame.sampled_at-std::chrono::milliseconds(151)),"old render pose cannot leave invisible shield");
  auto changed=owner;++changed.revision;
  check(!fresh(changed,frame.sampled_at),"release or transfer invalidates previous shield pose");
  ++input.reference_generation;check(!fresh(owner,frame.sampled_at),"recenter invalidates defense pose");--input.reference_generation;
  input.orientation_settling=true;check(!fresh(owner,frame.sampled_at),"tracking jump settling suspends shield");input.orientation_settling=false;
  input.grip[0].valid=false;check(!fresh(owner,frame.sampled_at),"lost holding-hand tracking suspends shield");input.grip[0].valid=true;
  input.focused=false;check(!fresh(owner,frame.sampled_at),"focus loss cannot retain protection");
  std::array<std::uint8_t,24> priorities;priorities.fill(5);const auto original_priorities=priorities;
  const auto filtered=shield::without_shield(priorities);
  check(priorities==original_priorities && filtered[19]==0,"native part priorities copied, never mutated");
  for(unsigned i=0;i<priorities.size();++i)if(i!=19)check(filtered[i]==priorities[i],"body hit-location priority preserved");
  shield::trace_stamps stamps;shield::bullet_result result{};shield::bullet_parameters parameters{};
  result.fraction=.5f;result.location=19;
  stamps.publish(&result,&parameters,owner);
  check(stamps.consume(&result,&parameters).id()==owner.id() && !stamps.consume(&result,&parameters).id(),"shield hit commits once before nested callbacks");
  stamps.publish(&result,&parameters,owner);result.fraction=.6f;
  check(!stamps.consume(&result,&parameters).id(),"copied or modified native result cannot inherit shield blocking");
  stamps.publish(&result,&parameters,owner);stamps.invalidate(&result);
  check(!stamps.consume(&result,&parameters).id(),"reused trace buffer invalidates old shield contact");
  std::array<shield::bullet_result,17> nested{};
  for(auto& r:nested)stamps.publish(&r,&parameters,owner);
  check(!stamps.consume(&nested.front(),&parameters).id() && stamps.consume(&nested.back(),&parameters).id()==owner.id(),"nested trace cache has a bounded lifetime without aliasing evicted contacts");
 }
}
