#pragma once
#include "forearm_twist.hpp"
#include "hands/pose_math.hpp"
#include "../pose_filter.hpp"

namespace vr::gameplay::scripted_arms
{
    inline int arm_owner(const hands::rig& rig,int bone) noexcept
    {
        for(int h=0;h<2;++h)if(hands::descendant(bone,rig.arms[h].shoulder,rig))return h;
        return -1;
    }
    inline bool requests_arm_pose(const hands::rig& rig,std::span<const std::uint32_t> requested) noexcept
    {
        if(rig.count<=0 || rig.count>255 || requested.size()!=8)return false;
        for(int b=0;b<rig.count;++b)
            if((requested[b/32]&(0x80000000u>>(b%32))) && arm_owner(rig,b)>=0)return true;
        return false;
    }
    inline bool pose_attached_prop(const hands::rig& rig,int root,unsigned hand,hands::anchor local,std::span<hands::bone> pose) noexcept
    {
        using namespace hands;
        if(hand>=2 || rig.count<=0 || rig.count>255 || root<0 || root>=rig.count || pose.size()<size_t(rig.count))return false;
        const auto wrist=rig.arms[hand].wrist;
        if(wrist<0 || wrist>=rig.count || root==wrist || !descendant(root,wrist,rig) ||
            !finite_twist_pose({local.rotation,local.position,2.f}))return false;
        for(int b=0;b<rig.count;++b)if(!finite_twist_pose(pose[b]))return false;
        pose_math::move_part(rig,root,pose_math::compose(pose_math::as_anchor(pose[wrist]),local),pose);
        return true;
    }
    // Blend local transforms after IK. Blending wrists alone would immediately
    // snap native elbows to the procedural pole, even at nearly zero weight.
    // Parent-space composition keeps native fingers and attached props coherent.
    inline bool blend_pose(const hands::rig& rig,std::span<const hands::bone> source,
        std::span<const hands::bone> solved,const std::array<float,2>& weight,std::span<hands::bone> output) noexcept
    {
        using namespace hands;
        if(rig.count<=0 || rig.count>255 || source.size()<size_t(rig.count) || solved.size()<size_t(rig.count) ||
            output.size()<size_t(rig.count))return false;
        for(float x:weight)if(!std::isfinite(x) || x<0 || x>1)return false;
        for(const auto& arm:rig.arms)
            if(arm.shoulder<0 || arm.shoulder>=rig.count || source[arm.shoulder].position!=solved[arm.shoulder].position)return false;
        for(int i=0;i<rig.count;++i)
            if(rig.parent[i]<-1 || rig.parent[i]>=i || !finite_twist_pose(source[i]) || !finite_twist_pose(solved[i]))return false;
        std::array<bone,256> result{};
        for(int i=0;i<rig.count;++i)
        {
            const int h=arm_owner(rig,i);result[i]=source[i];
            if(h<0 || weight[h]==0)continue;
            if(weight[h]==1){result[i]=solved[i];continue;}
            const auto parent=rig.parent[i];const float t=weight[h];
            const auto local=[&](std::span<const bone> pose)
            {
                const auto& b=pose[i];if(parent<0)return anchor{b.position,normalize(b.rotation)};
                const auto& p=pose[parent];const auto inverse=conjugate(normalize(p.rotation));
                return anchor{rotate(inverse,sub(b.position,p.position)),normalize(multiply(inverse,normalize(b.rotation)))};
            };
            const auto a=local(source),b=local(solved);
            auto position=add(scale(a.position,1-t),scale(b.position,t));
            auto rotation=pose_filter::slerp(a.rotation,b.rotation,t);
            if(parent>=0)
            {
                const auto q=normalize(result[parent].rotation);
                position=add(result[parent].position,rotate(q,position));rotation=normalize(multiply(q,rotation));
            }
            // The clavicles, body and authored shoulder positions do not follow HMD anchors.
            if(i==rig.arms[h].shoulder)position=source[i].position;
            result[i]={rotation,position,2.f};
        }
        std::copy_n(result.begin(),rig.count,output.begin());return true;
    }
}
