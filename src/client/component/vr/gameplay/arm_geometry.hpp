#pragma once
#include "hands/pose_solver.hpp"
namespace vr::gameplay::hands
{
 struct arm_geometry
 {
  std::array<vec,3> joints{}; // Actual solved shoulder, elbow, wrist in world space.
  float units_per_meter{};
  bool valid{};
 };
 inline arm_geometry capture_arm(const rig& r,std::span<const bone> pose,int hand,vec origin,float units) noexcept
 {
  if(hand<0 || hand>1 || r.count<=0 || r.count>256 || pose.size()<std::size_t(r.count) ||
   !std::isfinite(units) || units<=0 || units>10000)return {};
  const auto limb=r.arms[hand];const std::array indices{limb.shoulder,limb.elbow,limb.wrist};arm_geometry out;
  for(unsigned i=0;i<3;++i)
  {
   if(indices[i]<0 || indices[i]>=r.count)return {};
   out.joints[i]=add(pose[indices[i]].position,origin);
   for(float x:out.joints[i])if(!std::isfinite(x) || std::abs(x)>1e7f)return {};
  }
  for(unsigned i=0;i<2;++i)if(length(sub(out.joints[i+1],out.joints[i]))<.001f)return {};
  out.units_per_meter=units;out.valid=true;return out;
 }
}
