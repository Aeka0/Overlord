#pragma once
#include "hands/pose_solver.hpp"
#include <cstdint>

namespace vr::gameplay::free_climb
{
    using namespace hands;
    inline bool finite(vec value) noexcept
    {for(float x:value)if(!std::isfinite(x) || std::abs(x)>1e6f)return false;return true;}
    inline bool rotation_valid(quat value) noexcept
    {float n{};for(float x:value){if(!std::isfinite(x))return false;n+=x*x;}return n>.5f && n<1.5f;}
    struct contact {vec point{},normal{};bool valid{};}; // world meters
    struct hand {vec motion{},tip{};quat rotation{0,0,0,1};bool valid{},fixed{true};vec intent{};};
    struct support
    {
        contact surface{};quat rotation{0,0,0,1};
        vec previous{},goal{},release_origin{};
        float withdrawal{};bool sampled{},released{};
    };
    struct result {vec goal{};unsigned attached{},detached{};bool falling{};};
    inline bool fixed(bool trigger_active,bool trigger_down,bool grip_active,bool grip_down) noexcept
    {return !trigger_active || !grip_active || trigger_down || grip_down;}
    class solver
    {
        std::uint64_t reference_{};
        std::array<vec,2> previous_tip_{};
        std::array<vec,2> previous_motion_{};
        std::array<bool,2> sampled_{};
    public:
        std::array<support,2> hands{};
        static constexpr float extraction=.025f;
        void reset() noexcept {*this={};}
        unsigned held() const noexcept {return unsigned(hands[0].surface.valid)|(unsigned(hands[1].surface.valid)<<1);}
        void rebase() noexcept
        {for(auto& h:hands){h.sampled=false;h.released=false;h.withdrawal=0;}sampled_={};}
        void seed(unsigned h,contact point,quat rotation) noexcept
        {if(h<2 && point.valid && finite(point.point) && finite(point.normal) && std::abs(length(point.normal)-1.f)<.01f && rotation_valid(rotation))hands[h]={point,normalize(rotation)};}
        void obstructed(vec actual) noexcept
        {for(auto& h:hands)if(h.surface.valid)h.goal=actual;}
        void rescale(float ratio) noexcept
        {for(auto& h:hands)if(h.surface.valid)h.surface.point=scale(h.surface.point,ratio);rebase();}
        result update(vec actual,std::uint64_t reference,float dt,const std::array<hand,2>& input,
            const std::array<contact,2>& contacts) noexcept
        {
            result out{actual};
            if(!finite(actual) || !reference || !std::isfinite(dt) || dt<=0 || dt>.15f){rebase();return out;}
            if(reference_!=reference){reference_=reference;rebase();}
            const unsigned before=held();vec sum{};unsigned count{};
            for(unsigned h=0;h<2;++h)
            {
                const auto& in=input[h];auto& s=hands[h];
                if(!in.valid || !finite(in.motion) || !finite(in.tip) || !finite(in.intent) || !rotation_valid(in.rotation))
                {s.sampled=false;s.released=false;s.withdrawal=0;sampled_[h]=false;continue;}
                const bool continuous=sampled_[h] && length(sub(in.motion,previous_motion_[h]))<=.5f;
                if(!continuous){s.sampled=false;s.released=false;s.withdrawal=0;}
                if(s.surface.valid)
                {
                    if(!s.sampled || length(sub(in.motion,s.previous))>.5f)
                    {s.goal=actual;s.previous=in.motion;s.released=false;s.withdrawal=0;}
                    if(in.fixed){s.released=false;s.withdrawal=0;}
                    else if(!s.released){s.released=true;s.release_origin=in.motion;}
                    const auto delta=sub(in.motion,s.previous);
                    const float normal_travel=dot(delta,s.surface.normal);
                    if(s.released && normal_travel<0)s.release_origin=in.motion;
                    if(s.released)s.withdrawal=std::max(0.f,dot(sub(in.motion,s.release_origin),s.surface.normal));
                    if(s.released && s.withdrawal>=extraction)
                    {s={};out.detached|=1u<<h;}
                    else
                    {
                        const auto pull=s.released ? sub(delta,scale(s.surface.normal,std::max(0.f,normal_travel))) : delta;
                        s.goal=sub(s.goal,pull);s.previous=in.motion;s.sampled=true;
                        sum=add(sum,s.goal);++count;
                    }
                }
                else
                {
                    const auto& c=contacts[h];
                    // Geometry comes from a native surface sweep; there is no
                    // route index, nearest scripted point or consumed-contact set.
                    if(in.fixed && continuous && c.valid && finite(c.point) && finite(c.normal) &&
                        std::abs(length(c.normal)-1.f)<.01f && length(sub(in.intent,previous_tip_[h]))/dt>=.08f &&
                        dot(sub(in.intent,previous_tip_[h]),c.normal)<-.0005f)
                    {
                        s={c,normalize(in.rotation),in.motion,actual,{},0,true,false};
                        out.attached|=1u<<h;sum=add(sum,actual);++count;
                    }
                }
                previous_tip_[h]=in.intent;previous_motion_[h]=in.motion;sampled_[h]=true;
            }
            if(count)out.goal=scale(sum,1.f/float(count));
            out.falling=before && !held() && out.detached;
            return out;
        }
    };
    inline vec bounded_step(vec current,vec target,float dt,float speed=2.5f) noexcept
    {
        const auto d=sub(target,current);const float n=length(d),limit=std::max(0.f,dt)*speed;
        return n>limit && n>0 ? add(current,scale(d,limit/n)) : target;
    }
    inline vec outside_surface(vec point,const contact& wall,float clearance) noexcept
    {
        if(!wall.valid)return point;
        const float d=dot(sub(point,wall.point),wall.normal);
        return d<clearance ? add(point,scale(wall.normal,clearance-d)) : point;
    }
}
