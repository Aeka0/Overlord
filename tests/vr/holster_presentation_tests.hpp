#pragma once
#include "component/vr/gameplay/holster_presentation.hpp"
#include "component/vr/gameplay/hands/attachment_pose.hpp"
#include "component/vr/gameplay/stowed_reload_pose.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/miniuzi/profile.hpp"
namespace holster_presentation_tests
{
 template<class Check> void run(Check check)
 {
  using namespace vr::gameplay;using namespace weapons;using namespace carry;using namespace hands;
  inventory state;const owned_instance inventory_items[]{{{10,100},{true,true}},{{11,101},{true,true}},{{12,102},{false,false}}};
  check(state.reconcile_instances(inventory_items),"holster fixture owns independent physical instances");
  const auto* stored=state.in_slot(location::right_waist);check(stored!=nullptr,"right waist fixture available");if(!stored)return;
  stowed_part part{stored->id,location::right_waist,{{2,-3,4},{0,0,0,1}}};
  check(current_storage(part,state),"current waist instance admits its model parts");
  auto replacement=part;++replacement.id.generation;
  check(!current_storage(replacement,state),"same weapon token from another lifetime cannot borrow a holster model");
  auto moved=state;check(moved.equip(part.id,hand::left) && !current_storage(part,moved),"drawn weapon immediately stops contributing an old stowed model");
  replacement=part;replacement.at=location::left_waist;
  check(!current_storage(replacement,state),"old waist location cannot survive an exchange");
  attachments::solved pose;pose.reference=7;pose.sequence=10;pose.units=40;pose.head={1,2,4};
  pose.head_axis={vec{1,0,0},vec{0,1,0},vec{0,0,1}};pose.body={true,{-2,1,3},pose.head_axis};
  const vec origin{100,200,300};holster_layout layout;anchor first;
  check(place_stowed(part,attachments::body_frame(pose,origin),layout,first),"holster can use a hands-only body record without a muzzle identity");
  const auto body=attachments::body_frame(pose,origin);
  const auto slots=locate_holsters(body,layout);
  const auto expected=vr::gameplay::hands::pose_math::compose({slots.centers[1],holster_rotation(pose.body.yaw_axis,part.at)},part.local);
  check(length(sub(first.position,expected.position))<.0001f,"part uses estimated body and current record placement origin");
  // Keep the server inventory/local part completely unchanged through many
  // render samples: motion must not wait for the next server publication.
  for(int frame=1;frame<=20;++frame)
  {
   auto current=pose;const vec delta{float(frame)*.3f,float(frame)*.1f,-float(frame)*.05f};
   current.head=add(current.head,delta);current.body.position=add(current.body.position,delta);++current.sequence;
   anchor world;check(place_stowed(part,attachments::body_frame(current,origin),layout,world) && length(sub(world.position,add(first.position,delta)))<.0002f,
    "waist follows every render body translation while server metadata is unchanged");
  }
  auto rebased=pose;const vec shift{350,-600,10};rebased.head=sub(rebased.head,shift);rebased.body.position=sub(rebased.body.position,shift);
  anchor same;check(place_stowed(part,attachments::body_frame(rebased,add(origin,shift)),layout,same) && length(sub(first.position,same.position))<.0002f,
   "changing native model-origin representation does not cause a world-space hop");
  auto turned=pose;turned.body.yaw_axis={vec{0,1,0},vec{-1,0,0},vec{0,0,1}};
  anchor rotated;check(place_stowed(part,attachments::body_frame(turned,origin),layout,rotated) &&
   length(sub(rotate(rotated.rotation,{1,0,0}),rotate(first.rotation,{1,0,0})))>.1f,"waist orientation follows that same record's body yaw");
  anchor old;check(place_stowed(part,attachments::body_frame(pose,origin),layout,old) && old.position==first.position,
   "queued older render record uses its own body instead of the latest render sample");
  for(const auto hidden:{location::back,location::overflow,location::held,location::absent})
  {auto p=part;p.at=hidden;check(!place_stowed(p,body,layout,old),"only waist storage is visible; hidden back storage stays hidden");}
  auto corrupt=pose;corrupt.body.position[0]=NAN;
  check(!place_stowed(part,attachments::body_frame(corrupt,origin),layout,old),"invalid record body cannot publish a placement");
  // Preserve the existing view/world muzzle bridge and authored grip offset.
  const anchor bridge{{8,2,-1},normalize(quat{.2f,.3f,.1f,.8f})},model_part{{1,4,-2},{0,0,0,1}};
  const vec grip{3,1,-2};auto composed=part;composed.local=vr::gameplay::hands::pose_math::compose({scale(grip,-1),{0,0,0,1}},vr::gameplay::hands::pose_math::compose(bridge,model_part));
  anchor actual;check(place_stowed(composed,body,layout,actual),"composite waist part placed");
  const auto rotation=holster_rotation(pose.body.yaw_axis,part.at);
  const auto legacy=vr::gameplay::hands::pose_math::compose(vr::gameplay::hands::pose_math::compose({sub(slots.centers[1],rotate(rotation,grip)),rotation},bridge),model_part);
  check(length(sub(actual.position,legacy.position))<.0002f && length(sub(rotate(actual.rotation,{1,0,0}),rotate(legacy.rotation,{1,0,0})))<.0001f,
   "render-time placement retains existing grip alignment and composite part transforms");
  // Cold initialization, reload and either drawing hand share one authored
  // pivot. No held scene, muzzle sample or controller tracking is supplied.
  anchor cold;
  check(place_stowed_weapon(*stored,m9::base,body,layout,cold),"first-use stowed skeleton resolves its authored grip without a held cache");
  const auto wrist=vr::gameplay::hands::pose_math::compose(cold,m9::base.wrists[1]);
  check(length(sub(wrist.position,slots.centers[1]))<.0002f && hit(slots,wrist.position)==location::right_waist,
   "cold rendered grip coincides with the existing holster interaction anchor");
  for(const auto actor:{hand::left,hand::right})
  {
   auto cycled=state;check(cycled.equip(stored->id,actor),"draw original stored instance");
   const auto released=cycled.release(stored->id,1u<<unsigned(actor),stored->at,true,[](const auto&){return false;});
   check(released.action==outcome::stowed,"put the weapon back in its original waist slot");
   anchor warm;const auto* item=cycled.find(stored->id);
   check(item && place_stowed_weapon(*item,m9::base,body,layout,warm) && length(sub(warm.position,cold.position))<.0002f,
    "drawing with either hand cannot change the stowed grip pivot");
  }
  auto restarted=*stored;++restarted.id.generation;anchor fresh;
  check(place_stowed_weapon(restarted,m9::base,body,layout,fresh) && fresh.position==cold.position,
   "checkpoint replacement needs no pose from the previous instance");
  const auto shown=visible_instances(state,false);
  check(std::count_if(shown.begin(),shown.end(),[](const auto& v){return bool(v.id);})==2,
   "both waist weapons are admitted even with empty hands and legacy held rendering");
  auto both=state;check(both.equip(stored->id,hand::right) && both.support(stored->id,hand::left),"two-hand visible fixture");
  const auto held_and_stored=visible_instances(both);
  check(std::count_if(held_and_stored.begin(),held_and_stored.end(),[&](const auto& v){return v.id==stored->id;})==1,
   "two-hand ownership submits a weapon once");

  using namespace physical_reload;
  const auto id=stored->id;
  auto empty=stowed_ammunition(id,m9::physical,{},0);
  check(empty && empty->magazine_inserted && empty->action==mechanics::action_state::locked_open,
   "cold empty feed previews the same locked slide as native mechanical admission");
  presentation live;live.active=true;live.definition=&m9::physical;live.owner.weapon=id.weapon;live.owner.instance_generation=id.generation;
  live.ammo=*empty;live.ammo.magazine_inserted=false;
  const auto absent=stowed_ammunition(id,m9::physical,live,15);
  check(absent && !absent->magazine_inserted && absent->action==mechanics::action_state::locked_open,
   "native loaded fallback cannot refill or close an existing mechanical instance");
  auto stale=live;++stale.owner.instance_generation;
  check(!stowed_ammunition(id,m9::physical,stale,15),"another physical copy cannot supply stored feed state");
  stale=live;stale.definition=&miniuzi::physical;
  check(!stowed_ammunition(id,m9::physical,stale,15) && !stowed_ammunition(id,m9::physical,{},-1),
   "wrong profile and unavailable native counts are rejected without inventing feed state");
  rig r;r.count=7;r.gun=0;r.parent={};r.parent[0]=-1;r.parent[2]=1;
  for(int b=0;b<r.count;++b)r.weapon_bones[b]=true;
  part_rig parts;parts.valid=true;parts.magazine=1;parts.bullets=2;parts.slide=3;parts.bolt=4;
  parts.bullet_mask[0]=0x80000000u>>2;parts.animation_only_magazines[0]=0x80000000u>>5;
  const auto make_pose=[&] {std::array<bone,7> p{};for(auto& b:p)b.rotation={0,0,0,1};p[0].position=cold.position;p[0].rotation=cold.rotation;return p;};
  auto locked=make_pose();const auto without=pose_stowed(r,parts,m9::physical,*absent,40,locked);
  const auto bit=[](const part_mask& mask,int b){return (mask[b/32]&(0x80000000u>>(b%32)))!=0;};
  check(without.valid && bit(without.hidden,1) && bit(without.hidden,2) && bit(without.hidden,5) && !bit(without.hidden,0),
   "removed magazine, its rounds and animation-only spare disappear while the receiver stays visible");
  const auto expected_slide=vr::gameplay::hands::pose_math::compose(cold,handle_pose(m9::physical.slide_rest,nullptr,
   m9::physical.interaction.slide_axis,minimum_slide_travel(m9::physical.interaction,m9::physical.ammunition,*absent)*40,0));
  check(length(sub(locked[3].position,expected_slide.position))<.0002f,"empty locked slide retains its authored rear stop at the waist");
  auto loaded=stowed_ammunition(id,m9::physical,{},15);auto closed=make_pose();
  const auto with_mag=pose_stowed(r,parts,m9::physical,*loaded,40,closed);
  check(with_mag.valid && !bit(with_mag.hidden,1) && length(sub(closed[3].position,locked[3].position))>.5f,
   "another copy may independently retain an inserted magazine and closed slide");
  const auto revision=absent->revision;const auto total=mechanics::total_rounds(*absent);
  for(int frame=0;frame<100;++frame){auto p=make_pose();check(pose_stowed(r,parts,m9::physical,*absent,40,p).valid,"repeated stored render is read-only");}
  check(absent->revision==revision && mechanics::total_rounds(*absent)==total,"stowed rendering never advances mechanics or ammunition");
  auto open=stowed_ammunition(id,miniuzi::physical,{},32);auto uzi=make_pose();
  const auto open_pose=pose_stowed(r,parts,miniuzi::physical,*open,40,uzi);
  check(open_pose.valid && open->action==mechanics::action_state::cocked_open &&
   length(sub(uzi[4].position,vr::gameplay::hands::pose_math::compose(cold,miniuzi::internal_bolt.rest).position))>.5f,
   "open-bolt waist weapon preserves the internal bolt sear independently of the charging handle");
  check(!pose_stowed(r,parts,m9::physical,*absent,NAN,locked).valid,"invalid world scale cannot publish a stored mechanical pose");
 }
}
