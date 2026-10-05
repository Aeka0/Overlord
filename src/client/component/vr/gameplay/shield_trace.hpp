#pragma once
#include "shield_geometry.hpp"
#include "weapon_holding.hpp"
#include "../controller_input.hpp"
#include <cstring>

namespace vr::gameplay::weapons::shield
{
 // BulletProcess's means==3 branch dispatches explosive damage at 0x1404abedf.
 // Keep that native policy out of the initial ballistic shield adapter.
 inline bool ballistic_damage(int means) noexcept {return means==1 || means==2;}
 // H2 BulletFireParams and BulletTraceResults. Native-owned padding is retained
 // in stamps; synthetic results are zero-initialized before documented fields.
 struct bullet_parameters
 {
  std::uint16_t shooter,ignore;float scale;int means;bool flag;std::byte pad[3];
  vec original,start,end,direction;
 };
 struct bullet_result
 {
  float fraction;vec normal;int surface,contents,type;std::uint16_t entity;std::byte pad[2];
  unsigned part;std::uint16_t model,location;std::byte tail[8];
  void* target;vec position;bool ignored;std::byte reserved[3];int surface_type;
 };
 static_assert(offsetof(bullet_parameters,start)==0x1c && offsetof(bullet_parameters,direction)==0x34);
 static_assert(offsetof(bullet_result,target)==0x30 && offsetof(bullet_result,location)==0x26 && sizeof(bullet_result)==0x50);
 inline auto without_shield(std::array<std::uint8_t,24> priorities) noexcept
 {priorities[19]=0;return priorities;}
 inline bool pose_usable(const hold& actual,const hold& expected,std::uint64_t sequence,std::uint64_t reference,
  controller_input::clock::time_point at,const controller_input::frame& input,controller_input::clock::time_point now) noexcept
 {
  const auto hand=expected.holding_hand();
  return expected.id() && valid_hand(hand) && actual.id()==expected.id() && actual.revision==expected.revision &&
   actual.rear==expected.rear && actual.support==expected.support && actual.rear_revision==expected.rear_revision &&
   sequence && sequence<=input.sequence && reference==input.reference_generation && now>=at && now-at<=std::chrono::milliseconds(150) &&
   input.focused && !input.orientation_settling && now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150) &&
   input.grip[unsigned(hand)].valid && input.aim[unsigned(hand)].valid;
 }
 class trace_stamps
 {
  struct stamp {bullet_result* result{};bullet_parameters* parameters{};bullet_result value{};hold owner{};};
  std::array<stamp,16> entries{};unsigned cursor{};
 public:
  void invalidate(bullet_result* result) noexcept {for(auto& s:entries)if(s.result==result)s={};}
  void publish(bullet_result* result,bullet_parameters* parameters,hold owner) noexcept
  {invalidate(result);if(result && parameters && owner.id())entries[cursor++%entries.size()]={result,parameters,*result,owner};}
  hold consume(bullet_result* result,bullet_parameters* parameters) noexcept
  {
   for(auto& s:entries)if(s.result==result && s.parameters==parameters)
   {
    const auto owner=result && std::memcmp(&s.value,result,sizeof(*result))==0 ? s.owner : hold{};
    s={};return owner;
   }
   return {};
  }
 };
}
