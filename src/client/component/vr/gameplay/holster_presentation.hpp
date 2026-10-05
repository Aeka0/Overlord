#pragma once
#include "weapon_holsters.hpp"
#include "weapon_profile.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"
namespace vr::gameplay::weapons::carry
{
 struct stowed_part {identity id{};location at{location::absent};hands::anchor local{};};
 inline bool visible_storage(location at) noexcept {return at==location::left_waist || at==location::right_waist;}
 inline constexpr std::size_t visible_instance_capacity=4; // Two held weapons and two waist slots.
 inline std::array<instance,visible_instance_capacity> visible_instances(const inventory& state,bool include_held=true) noexcept
 {
  std::array<instance,visible_instance_capacity> out{};std::size_t count{};
  for(const auto& item:state.instances())
   if(item.id && (visible_storage(item.at) || (include_held && item.at==location::held)) && count<out.size())out[count++]=item;
  return out;
 }
 inline bool current_storage(const stowed_part& part,const inventory& state) noexcept
 {const auto* live=state.find(part.id);return part.id && visible_storage(part.at) && live && live->at==part.at;}
 inline bool place_stowed(const stowed_part& part,const head_pose_bridge::spatial_frame& body,
  const holster_layout& layout,hands::anchor& world) noexcept
 {
  if(!part.id || !visible_storage(part.at))return false;
  for(float x:part.local.position)if(!std::isfinite(x) || std::abs(x)>10000)return false;
  float norm{};for(float x:part.local.rotation){if(!std::isfinite(x))return false;norm+=x*x;}
  if(std::abs(norm-1)>.002f)return false;
  const auto slots=locate_holsters(body,layout);if(!slots.valid)return false;
  const auto index=part.at==location::left_waist ? 0 : 1;
  world=hands::pose_math::compose({slots.centers[index],holster_rotation(head_pose_bridge::body_slots_frame(body).head_yaw_axis,part.at)},part.local);
  return true;
 }
 // The authored control is gun-local and exists before the first held pose.
 // A stowed skeleton uses the same view asset as the hand, so it needs no
 // learned view/world muzzle bridge and never inherits a left-hand mirror.
 inline bool place_stowed_weapon(const instance& item,const profile& source,
  const head_pose_bridge::spatial_frame& body,const holster_layout& layout,hands::anchor& world) noexcept
 {
  const auto grip=source.control_grips ? (*source.control_grips)[1].position : source.wrists[1].position;
  return place_stowed({item.id,item.at,{hands::scale(grip,-1),{0,0,0,1}}},body,layout,world);
 }
}
