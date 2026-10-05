#pragma once
#include "hand_pose_solver.hpp"
namespace vr::gameplay::hands
{
 struct forearm_mount {vec axis{};}; // Authored wrist -> elbow direction in model space.
 struct mounted_limb {limb_solution limb{};quat rotation{};};
 inline mounted_limb mount_forearm(const forearm_mount& mount,float upper,float lower,anchor wrist,vec shoulder,
  const std::array<vec,3>& body_axis,int hand) noexcept
 {
  float norm{};for(float x:wrist.rotation){if(!std::isfinite(x))return {};norm+=x*x;}
  if(std::abs(norm-1)>.002f || !std::isfinite(length(mount.axis)) || std::abs(length(mount.axis)-1)>.002f)return {};
  const auto expected=rotate(wrist.rotation,mount.axis),hint=add(wrist.position,expected);
  const auto limb=solve_limb(upper,lower,wrist.position,shoulder,body_axis,hand,&hint);
  if(!limb.valid)return {};
  // Swing the strapped object around its grip onto the solved forearm. Keep
  // wrist roll, bone lengths and the physical handle; never translate shoulders.
  return {limb,normalize(multiply(from_to(expected,sub(limb.elbow,limb.wrist)),wrist.rotation))};
 }
 inline bool apply_mounted_limb(const rig& r,std::span<bone> pose,int hand,const mounted_limb& mounted,vec shoulder,quat wrist_rotation) noexcept
 {
  if(!mounted.limb.valid || hand<0 || hand>1 || r.count<=0 || r.count>256 || pose.size()<std::size_t(r.count))return false;
  const auto a=r.arms[hand];if(a.shoulder<0 || a.elbow<=a.shoulder || a.wrist<=a.elbow || a.wrist>=r.count)return false;
  const auto s=pose[a.shoulder],e=pose[a.elbow],w=pose[a.wrist];
  const auto rotations=orient_arm(sub(e.position,s.position),sub(w.position,e.position),
   sub(mounted.limb.elbow,shoulder),sub(mounted.limb.wrist,mounted.limb.elbow),s.rotation);
  const auto upper=rotations.upper,lower=rotations.lower;
  const auto fingers=normalize(multiply(wrist_rotation,conjugate(normalize(w.rotation))));
  for(int i=0;i<r.count;++i)
  {
   if(r.weapon_bones[i])continue;
   if(descendant(i,a.wrist,r))pose[i]=transformed(pose[i],w.position,mounted.limb.wrist,fingers);
   else if(descendant(i,a.elbow,r))pose[i]=transformed(pose[i],e.position,mounted.limb.elbow,lower);
   else if(descendant(i,a.shoulder,r))pose[i]=transformed(pose[i],s.position,shoulder,upper);
  }
  return true;
 }
}
