#pragma once
#include "free_climb.hpp"

namespace vr::gameplay::free_climb
{
    struct volume {vec midpoint{},half{};};
    inline bool hanging_volume(volume standing,float units,volume& out) noexcept
    {
        if(!std::isfinite(units) || units<=0 || !finite(standing.midpoint) || !finite(standing.half))return false;
        for(float x:standing.midpoint)if(std::abs(x)>128)return false;
        for(float x:standing.half)if(x<=0 || x>128)return false;
        out=standing;
        out.half[0]=std::min(out.half[0],.22f*units);
        out.half[1]=std::min(out.half[1],.22f*units);
        out.half[2]=std::min(out.half[2],.45f*units);
        // Keep the original head clearance; tuck the unsupported lower body.
        out.midpoint[2]=standing.midpoint[2]+standing.half[2]-out.half[2];
        return true;
    }
    struct sweep_hit {float fraction{1};vec normal{};bool startsolid{},allsolid{};};
    struct slide_result
    {
        vec position{};std::array<vec,4> planes{};unsigned plane_count{},traces{};
        bool blocked{},stuck{};
    };
    inline vec clip_motion(vec motion,std::span<const vec> planes) noexcept
    {
        const auto original=motion;
        for(unsigned i=0;i<planes.size();++i)
        {
            const float into=dot(motion,planes[i]);if(into>=0)continue;
            motion=sub(motion,scale(planes[i],into));
            for(unsigned j=0;j<i;++j)if(dot(motion,planes[j])<-.0001f)
            {
                const auto crease=cross(planes[i],planes[j]);const float size=length(crease);
                if(size<.0001f)return {};
                const auto direction=scale(crease,1/size);motion=scale(direction,dot(original,direction));
                for(const auto& n:planes)if(dot(motion,n)<-.0001f)return {};
            }
        }
        return motion;
    }
    template<class Trace>slide_result slide(vec from,vec delta,float skin,Trace&& trace)
    {
        slide_result out;out.position=from;
        if(!finite(from) || !finite(delta) || !std::isfinite(skin) || skin<0){out.stuck=true;return out;}
        auto remaining=delta;
        for(unsigned attempt=0;attempt<4 && length(remaining)>.0001f;++attempt)
        {
            const auto hit=trace(out.position,add(out.position,remaining));++out.traces;
            if(!std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1 || hit.startsolid || hit.allsolid)
            {out.blocked=out.stuck=true;break;}
            const float distance=length(remaining);
            const float fraction=hit.fraction<1?std::max(0.f,hit.fraction-skin/distance):1.f;
            out.position=add(out.position,scale(remaining,fraction));
            if(hit.fraction==1)break;
            out.blocked=true;
            if(!finite(hit.normal) || length(hit.normal)<.9f){out.stuck=true;break;}
            out.planes[out.plane_count++]=unit(hit.normal);
            remaining=clip_motion(scale(remaining,1-fraction),{out.planes.data(),out.plane_count});
        }
        return out;
    }
}
