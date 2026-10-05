#pragma once
#include "free_climb.hpp"
#include <vector>
#include <utility>
#include <map>
#include <set>

namespace vr::gameplay::ladders
{
    using namespace hands;
    struct rung {vec a{},b{};float radius{.75f};};
    struct placement
    {
        vec origin{};std::array<vec,3> axis{{{1,0,0},{0,1,0},{0,0,1}}};float size{1};
        vec world(vec p) const noexcept
        {for(unsigned i=0;i<3;++i)p[i]*=size;return add(origin,add(scale(axis[0],p[0]),add(scale(axis[1],p[1]),scale(axis[2],p[2]))));}
        vec local(vec p) const noexcept
        {p=sub(p,origin);return {dot(p,axis[0])/size,dot(p,axis[1])/size,dot(p,axis[2])/size};}
    };
    inline vec closest(rung r,vec p) noexcept
    {
        const auto d=sub(r.b,r.a);const float n=dot(d,d);
        return n>1e-8f?add(r.a,scale(d,std::clamp(dot(sub(p,r.a),d)/n,.08f,.92f))):r.a;
    }
    // An actual indexed mesh edge supplies contact geometry, never the ladder
    // brush's AABB or equally spaced points inferred from its height.
    inline bool horizontal_edge(vec a,vec b) noexcept
    {
        if(!free_climb::finite(a) || !free_climb::finite(b))return false;
        const auto d=sub(b,a);const float horizontal=std::hypot(d[0],d[1]);
        return horizontal>=8 && horizontal<=80 && std::abs(d[2])<=std::max(.4f,horizontal*.035f);
    }
    // A rung family repeats in height on one plane. Horizontal rails in front
    // of that plane are separate geometry, even when their native trace hits
    // the same thin ladder brush. Collapse mesh facets to actual rung centres.
    inline std::vector<rung> repeated_rungs(std::span<const rung> edges)
    {
        struct layer {std::set<int> heights;float low{INFINITY},high{-INFINITY},zlow{INFINITY},zhigh{-INFINITY};};
        std::map<std::pair<unsigned,int>,layer> layers;
        const auto orientation=[](rung e)->unsigned {
            const auto d=unit(sub(e.b,e.a));return std::abs(d[1])>.98f?1u:std::abs(d[0])>.98f?0u:2u;
        };
        for(const auto& e:edges)
        {
            if(!horizontal_edge(e.a,e.b))continue;
            const auto along=orientation(e);if(along==2)continue;
            const auto mid=scale(add(e.a,e.b),.5f);const auto plane=mid[1-along];
            auto& l=layers[{along,int(std::floor(plane/2.5f))}];
            l.heights.insert(int(std::floor((mid[2]+.5f)/4.f)));
            l.low=std::min(l.low,plane);l.high=std::max(l.high,plane);l.zlow=std::min(l.zlow,mid[2]);l.zhigh=std::max(l.zhigh,mid[2]);
        }
        if(layers.empty())return {};
        auto best=layers.begin();
        for(auto it=layers.begin();it!=layers.end();++it)
            if(it->second.heights.size()>best->second.heights.size() ||
                (it->second.heights.size()==best->second.heights.size() && it->second.zhigh-it->second.zlow>best->second.zhigh-best->second.zlow))best=it;
        // An isolated rail does not establish a repeated climbing surface.
        if(best->second.heights.size()<3)return {};
        const auto along=best->first.first;const float plane=(best->second.low+best->second.high)*.5f;
        std::vector<rung> accepted;
        for(const auto& e:edges)if(horizontal_edge(e.a,e.b) && orientation(e)==along &&
            std::abs((e.a[1-along]+e.b[1-along])*.5f-plane)<=2.5f)accepted.push_back(e);
        std::sort(accepted.begin(),accepted.end(),[](rung a,rung b){return a.a[2]+a.b[2]<b.a[2]+b.b[2];});
        std::vector<rung> out;
        for(size_t i=0;i<accepted.size();)
        {
            vec low=accepted[i].a,high=low;const float height=(accepted[i].a[2]+accepted[i].b[2])*.5f;
            do {
                for(auto p:{accepted[i].a,accepted[i].b})for(unsigned j=0;j<3;++j){low[j]=std::min(low[j],p[j]);high[j]=std::max(high[j],p[j]);}
                ++i;
            }while(i<accepted.size() && (accepted[i].a[2]+accepted[i].b[2])*.5f-height<3.5f);
            vec a=scale(add(low,high),.5f),b=a;a[along]=low[along];b[along]=high[along];
            if(length(sub(b,a))>=8)out.push_back({a,b,std::clamp(std::max(high[1-along]-low[1-along],high[2]-low[2])*.5f,.35f,2.5f)});
        }
        return out;
    }
    // Render-only interpolation of the collision-approved body position. Head
    // tracking stays unfiltered; this value never feeds the movement solver.
    class camera_translation
    {
        vec value_{};double at_{};std::uint64_t reference_{};bool sampled_{};
    public:
        void reset()noexcept{*this={};}
        vec sample(vec target,std::uint64_t reference,double seconds,float units)noexcept
        {
            if(!free_climb::finite(target) || !reference || !std::isfinite(seconds) || !std::isfinite(units) || units<=0)return target;
            const auto dt=seconds-at_;
            if(!sampled_ || reference_!=reference || dt<0 || dt>.15 || length(sub(target,value_))>.5f*units)value_=target;
            else if(dt>0){const float alpha=float(-std::expm1(-.6931471805599453*dt/.018));value_=add(value_,scale(sub(target,value_),alpha));}
            at_=seconds;reference_=reference;sampled_=true;return value_;
        }
    };
    // Only this swept segment is admitted for late visual motion. Independent
    // XYZ clearance ranges would incorrectly admit untested diagonal corners.
    struct pull_window {vec direction{0,0,1};float down{},up{};bool valid{};};
    inline vec preview_pull(vec body,const std::array<vec,2>& deltas,unsigned held,pull_window window)noexcept
    {
        if(!window.valid || !held || !free_climb::finite(body) || !free_climb::finite(window.direction) ||
            std::abs(length(window.direction)-1.f)>.001f || !std::isfinite(window.down) || !std::isfinite(window.up) || window.down>0 || window.up<0)return body;
        float sum{};unsigned count{};
        for(unsigned h=0;h<2;++h)if(held&(1u<<h))
        {if(!free_climb::finite(deltas[h]))return body;sum-=dot(deltas[h],window.direction);++count;}
        if(count)body=add(body,scale(window.direction,std::clamp(sum/float(count),window.down,window.up)));
        return body;
    }
    inline constexpr float top_pull_meters=.08f;
    inline bool same_column(vec a,vec b,vec normal,float units) noexcept
    {
        const auto d=sub(a,b);return std::abs(dot(d,normal))<.25f*units &&
            std::abs(dot(d,unit(cross({0,0,1},normal))))<.75f*units && std::abs(d[2])<12*units;
    }
    // Ordinary hand grips release immediately, unlike ice-pick extraction.
    class pull_solver
    {
        free_climb::solver core_;
    public:
        unsigned held()const noexcept{return core_.held();}
        void clear()noexcept{core_.reset();}
        void rebase()noexcept{core_.rebase();}
        void obstructed(vec p)noexcept{core_.obstructed(p);}
        // Changing from two supports to one (or adding a second) must not
        // expose the surviving hand's historical, unaveraged target as a jump.
        void topology_changed(vec actual)noexcept{core_.obstructed(actual);}
        void attach(unsigned h,vec point,vec normal,quat rotation)noexcept
        {core_.seed(h,{point,normal,true},rotation);}
        void release(unsigned h)noexcept{if(h<2)core_.hands[h]={};}
        vec update(vec actual,std::uint64_t reference,float dt,const std::array<free_climb::hand,2>& input)noexcept
        {return core_.update(actual,reference,dt,input,{}).goal;}
    };
    struct exit_route {vec lift{},landing{};bool valid{};};
    template<class Sweep>bool clear_route(vec from,const exit_route& route,Sweep&& sweep)
    {
        if(!route.valid || !free_climb::finite(from) || !free_climb::finite(route.lift) || !free_climb::finite(route.landing))return false;
        for(const auto& ends:{std::pair{from,route.lift},std::pair{route.lift,route.landing}})
        {const auto h=sweep(ends.first,ends.second);if(!std::isfinite(h.fraction) || h.fraction!=1 || h.startsolid || h.allsolid)return false;}
        return true;
    }
}
