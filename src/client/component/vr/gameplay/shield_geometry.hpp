#pragma once
#include "component/vr/gameplay/hand_pose_math.hpp"
#include "arm_geometry.hpp"

namespace vr::gameplay::weapons::shield
{
 using hands::vec;
 struct triangle {vec a,b,c;}; // Counterclockwise from the protective side.
 struct profile {std::span<const triangle> panels;std::array<vec,9> melee_points;};
 struct contact {bool valid{};float fraction{1};vec point{},normal{};};
 enum class faces {front,both};
 inline bool finite(vec v) noexcept
 {return std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x) && std::abs(x)<1e7f;});}
 // Finite segments, not attacker-to-player bearings. The nearest original
 // obstruction is the upper bound, so a shield cannot protect through a wall.
 inline contact intersect(const profile& p,hands::anchor world,vec start,vec end,float limit=1,faces side=faces::front) noexcept
 {
  using namespace hands;
  if(p.panels.empty() || p.panels.size()>64 || !finite(world.position) || !finite(start) || !finite(end) ||
   !std::isfinite(limit) || limit<=0 || limit>1)return {};
  float norm{};for(float x:world.rotation){if(!std::isfinite(x))return {};norm+=x*x;}
  if(std::abs(norm-1)>0.002f)return {};
  const auto inverse=conjugate(world.rotation);
  const auto origin=rotate(inverse,sub(start,world.position)),delta=rotate(inverse,sub(end,start));
  const auto distance=length(delta);if(!std::isfinite(distance) || distance<.0001f)return {};
  contact result;result.fraction=limit;
  for(const auto& t:p.panels)
  {
   if(!finite(t.a) || !finite(t.b) || !finite(t.c))return {};
   const auto e1=sub(t.b,t.a),e2=sub(t.c,t.a),normal=cross(e1,e2);
   const float area=length(normal);if(!std::isfinite(area) || area<.0001f)return {};
   const auto q=cross(delta,e2);const float det=dot(e1,q);
   if((side==faces::front ? det : std::abs(det))<=area*distance*1e-6f)continue;
   const auto offset=sub(origin,t.a);const float u=dot(offset,q)/det;
   const auto r=cross(offset,e1);const float v=dot(delta,r)/det;
   const float fraction=dot(e2,r)/det;
   if(u< -1e-6f || v< -1e-6f || u+v>1.000001f || fraction<=0 || fraction>=result.fraction)continue;
   result={true,fraction,add(start,scale(sub(end,start),fraction)),rotate(world.rotation,scale(normal,(det<0?-1.f:1.f)/area))};
  }
  return result;
 }
 inline bool valid_surface(const profile& p,hands::anchor world) noexcept
 {
  using namespace hands;
  if(p.panels.empty() || p.panels.size()>64 || !finite(world.position))return false;
  float norm{};for(float x:world.rotation){if(!std::isfinite(x))return false;norm+=x*x;}if(std::abs(norm-1)>.002f)return false;
  for(const auto& panel:p.panels)
  {
   if(!finite(panel.a) || !finite(panel.b) || !finite(panel.c))return false;
   const float area=length(cross(sub(panel.b,panel.a),sub(panel.c,panel.a)));if(!std::isfinite(area) || area<.0001f)return false;
  }
  return true;
 }
 // Only physical penetration prevents a shot. A gun entirely behind the
 // shield fires normally; native outgoing traces handle its impact afterwards.
 inline bool barrel_penetrates(const profile& p,hands::anchor world,vec hand,vec muzzle) noexcept
 {
  using namespace hands;
  if(!valid_surface(p,world) || !finite(hand) || !finite(muzzle))return true;
  const auto barrel=sub(muzzle,hand);const auto distance=length(barrel);
  return distance>.0001f && intersect(p,world,sub(hand,scale(barrel,.01f/distance)),add(muzzle,scale(barrel,.01f/distance)),1,faces::both).valid;
 }
 inline float segment_distance_squared(vec a,vec b,vec c,vec d) noexcept
 {
  using namespace hands;
  const auto u=sub(b,a),v=sub(d,c),w=sub(a,c);
  const float aa=dot(u,u),bb=dot(u,v),cc=dot(v,v),dd=dot(u,w),ee=dot(v,w);
  float s{},t{};
  if(aa<=1e-10f)t=cc>1e-10f?std::clamp(ee/cc,0.f,1.f):0;
  else if(cc<=1e-10f)s=std::clamp(-dd/aa,0.f,1.f);
  else
  {
   const float denominator=aa*cc-bb*bb;
   if(denominator>1e-6f*aa*cc)s=std::clamp((bb*ee-cc*dd)/denominator,0.f,1.f);
   t=(bb*s+ee)/cc;
   if(t<0){t=0;s=std::clamp(-dd/aa,0.f,1.f);}
   else if(t>1){t=1;s=std::clamp((bb-dd)/aa,0.f,1.f);}
  }
  const auto delta=sub(add(a,scale(u,s)),add(c,scale(v,t)));return dot(delta,delta);
 }
 inline bool projected_inside(const triangle& t,vec point) noexcept
 {
  using namespace hands;const auto n=cross(sub(t.b,t.a),sub(t.c,t.a));
  return dot(cross(sub(t.b,t.a),sub(point,t.a)),n)>=0 && dot(cross(sub(t.c,t.b),sub(point,t.b)),n)>=0 &&
   dot(cross(sub(t.a,t.c),sub(point,t.c)),n)>=0;
 }
 inline float segment_panel_distance_squared(vec a,vec b,const triangle& t) noexcept
 {
  using namespace hands;
  const auto n=cross(sub(t.b,t.a),sub(t.c,t.a));const float area=dot(n,n);
  const float da=dot(sub(a,t.a),n),db=dot(sub(b,t.a),n);
  if(((da<=0 && db>=0) || (da>=0 && db<=0)) && std::abs(da-db)>1e-10f &&
   projected_inside(t,add(a,scale(sub(b,a),da/(da-db)))))return 0;
  float closest=std::min({segment_distance_squared(a,b,t.a,t.b),segment_distance_squared(a,b,t.b,t.c),segment_distance_squared(a,b,t.c,t.a)});
  for(const auto point:{a,b})
  {
   const float distance=dot(sub(point,t.a),n);
   if(projected_inside(t,sub(point,scale(n,distance/area))))closest=std::min(closest,distance*distance/area);
  }
  return closest;
 }
 enum class clipping {none,invalid,barrel,upper_arm,forearm};
 inline clipping fire_penetration(const profile& p,hands::anchor world,const hands::arm_geometry& arm,vec muzzle) noexcept
 {
  using namespace hands;
  if(!valid_surface(p,world) || !arm.valid || !std::isfinite(arm.units_per_meter) || arm.units_per_meter<=0 || arm.units_per_meter>10000)return clipping::invalid;
  for(const auto joint:arm.joints)if(!finite(joint))return clipping::invalid;
  if(barrel_penetrates(p,world,arm.joints[2],muzzle))return clipping::barrel;
  const auto inverse=conjugate(world.rotation);
  // Conservative arm capsules (3.5 cm upper arm, 2.5 cm forearm), independent
  // of the shield holder. Edges/endpoints/coplanar contact count as penetration.
  constexpr std::array radii{.035f,.025f};
  for(unsigned part=0;part<2;++part)
  {
   const auto a=rotate(inverse,sub(arm.joints[part],world.position)),b=rotate(inverse,sub(arm.joints[part+1],world.position));
   if(length(sub(a,b))<.001f)return clipping::invalid;
   const float radius=radii[part]*arm.units_per_meter;
   for(const auto& panel:p.panels)if(segment_panel_distance_squared(a,b,panel)<=radius*radius)
    return part==0?clipping::upper_arm:clipping::forearm;
  }
  return clipping::none;
 }
}
