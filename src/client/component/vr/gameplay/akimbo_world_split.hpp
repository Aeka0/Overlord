#pragma once
#include "world_pickup_policy.hpp"
#include <optional>

namespace vr::gameplay::weapons::carry
{
 // A native akimbo item is one definition with two clip payloads. Its loose
 // reserve belongs to the pickup once; the remaining gun keeps only clip two.
 struct world_ammo_payload
 {
  int reserve{},loaded{},second{};std::uint32_t flags{};
  bool operator==(const world_ammo_payload&)const=default;
 };
 struct akimbo_split
 {
  world_ammo_payload original,pickup,remainder;
  bool may_restore(world_ammo_payload current)const noexcept{return current==pickup;}
 };
 inline std::optional<akimbo_split> split_akimbo(world_ammo_payload p)noexcept
 {
  // -1 is the engine's authored/default-ammunition sentinel. Preserve it for
  // each clip separately rather than materializing or multiplying default ammo.
  const auto rounds=[](int n){return n>=-1 && n<=1000000;};
  if(!(p.flags&1) || !rounds(p.reserve) || !rounds(p.loaded) || !rounds(p.second))return {};
  auto single=p;single.flags&=~1u;
  auto remainder=single;remainder.reserve=0;remainder.loaded=p.second;remainder.second=0;
  return akimbo_split{p,single,remainder};
 }
 inline constexpr bool admit_akimbo_split(pickup_context context,bool supported_live_item,
  int automatic,int dual,std::uint32_t script_flags)noexcept
 {
  return (context==pickup_context::hand_query || context==pickup_context::grip_transfer) &&
   supported_live_item && automatic==0 && dual==1 && !(script_flags&0x8080u);
 }
}
