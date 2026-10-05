#pragma once
#include "component/vr/gameplay/akimbo_world_split.hpp"
#include "component/vr/gameplay/weapon_clip_projection.hpp"

namespace akimbo_split_tests
{
 template<class Check>void run(Check check)
 {
  using namespace vr::gameplay::weapons;using namespace carry;
  namespace p=native_ammunition::projection;namespace s=native_ammunition::storage;
  for(auto context:{pickup_context::hand_query,pickup_context::grip_transfer})
  {
   check(admit_akimbo_split(context,true,0,1,0),"akimbo world item enters hand query and explicit grip transfer");
   check(!admit_akimbo_split(context,false,0,1,0) && !admit_akimbo_split(context,true,1,1,0),"foreign/unsupported items and automatic touch cannot split");
   for(unsigned flags:{0x80u,0x8000u,0x8080u})check(!admit_akimbo_split(context,true,0,1,flags),"akimbo split respects native script pickup restrictions");
   for(int dual:{-1,0,2})check(!admit_akimbo_split(context,true,0,dual,0),"only validated native pair metadata enables splitting");
  }
  check(!admit_akimbo_split(pickup_context::native,true,0,1,0),"flat/native query has no akimbo override");
  for(bool owned:{false,true})for(int first:{0,1,7})for(int second:{0,2,7})
  {
   std::array<std::byte,s::extent> memory{};clip_ledger clips;
   const clip_ledger::definition seed[]{{21,3,21,0}};
   check(clips.reconcile(owned?std::span<const clip_ledger::definition>(seed):std::span<const clip_ledger::definition>{}),"seed optional existing physical gun");
   check(s::commit(memory,21,21,0,0,owned?3:0,10),"seed native shared reserve and optional clip");
   world_ammo_payload world{14,first,second,0x21};
   const auto plan=split_akimbo(world);check(bool(plan),"native dual clip payload splits");
   if(!plan)continue;
   const auto previous=clips.projected(21),a=clips.allocate(21);
   check(p::begin_pickup(clips,memory,21,21,21),"reserve first gun before world mutation");
   world=plan->pickup;
   check(!(world.flags&1) && world.second==second,"native grant and import see single mode with second clip escrow untouched");
   const auto before=s::observe(memory,21,21);
   check(s::commit(memory,21,21,before.clip.count,before.reserve.count,world.loaded,before.reserve.count+world.reserve),"native pickup imports first clip and reserve only");
   // The exact successful Touch_Item free is suppressed; entity/generation and
   // placement remain unchanged. The second hand can then pick up that entity.
   check(plan->may_restore(world),"successful native pickup still has the staged payload");world=plan->remainder;
   check(p::finish_pickup(clips,memory,a,previous,21,21,0,true) && clips.find(a)->loaded==first,"first single physical instance is committed");
   check(world.loaded==second && world.reserve==0 && world.second==0 && world.flags==0x20 && !split_akimbo(world),"remaining item is one single gun and cannot split a third copy");
   const auto b=clips.allocate(21),projected=clips.projected(21);
   check(p::begin_pickup(clips,memory,21,21,21),"second pickup reserves a different physical identity");
   const auto next=s::observe(memory,21,21);
   check(s::commit(memory,21,21,next.clip.count,next.reserve.count,world.loaded,next.reserve.count) &&
    p::finish_pickup(clips,memory,b,projected,21,21,0,true),"remaining gun uses ordinary pickup without repeating reserve grant");
   check(a!=b && clips.find(a)->loaded==first && clips.find(b)->loaded==second && clips.count(21)==size_t(owned?3:2) &&
    s::observe(memory,21,21).reserve.count==24 && (!owned || clips.find(previous)->loaded==3),"two sequential grabs conserve independent clips and preserve existing same-model gun");
  }
  for(int primary:{-1,0,7})for(int secondary:{-1,0,7})
  {
   const world_ammo_payload original{-1,primary,secondary,1};const auto plan=split_akimbo(original);
   check(plan && plan->pickup.loaded==primary && plan->remainder.loaded==secondary && plan->remainder.reserve==0,"default-ammo sentinels remain separate and reserve initializes only once");
  }
  const auto plan=split_akimbo({20,6,4,1});auto staged=plan->pickup;
  check(plan->may_restore(staged),"rejected native pickup can restore unchanged pair flag");
  staged.second=3;check(!plan->may_restore(staged),"script-altered payload is never overwritten by rollback or retained-world completion");
  check(!split_akimbo({0,1,1,0}) && !split_akimbo({0,-2,1,1}) && !split_akimbo({0,1,1000001,1}),"single and malformed pair payloads do not enter split transaction");
  std::array<std::byte,s::extent> memory{};clip_ledger full;
  for(size_t i=0;i<clip_ledger::capacity;++i)check(full.add(full.allocate(21),3,21,0),"fill physical capacity");
  const auto untouched=memory;auto world=plan->original;
  if(p::begin_pickup(full,memory,21,21,21))world=plan->pickup;
  check(world==plan->original && memory==untouched,"full physical inventory rejects before pair flag or ammunition changes");
 }
}
