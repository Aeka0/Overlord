#pragma once
#include "special_equipment_policy.hpp"

namespace vr::gameplay::equipment::special::flare
{
    using vr::hand;
    inline constexpr int burn_msec=10225;
    enum class phase { stowed, held, dropped, spent };
    struct state
    {
        phase stage{};hand holder{hand::none},opener{hand::none};
        std::uint64_t revision{};bool lit{},cap_removed{};
        hands::vec pull_start{};float travel{};
        bool held()const noexcept{return stage==phase::held && vr::valid_hand(holder);}
        bool take(hand actor)noexcept
        {
            if(stage!=phase::stowed || !vr::valid_hand(actor))return false;
            holder=actor;stage=phase::held;++revision;return true;
        }
        void cancel_pull()noexcept{opener=hand::none;pull_start={};travel=0;}
        bool start_pull(hand actor,hands::vec local,bool authorized)noexcept
        {
            if(!authorized || !held() || lit || cap_removed || vr::valid_hand(opener) ||
                !vr::valid_hand(actor) || actor==holder)return false;
            for(float x:local)if(!std::isfinite(x))return false;
            opener=actor;pull_start=local;travel=0;return true;
        }
        bool sample_pull(hands::vec local,float units,bool authorized)noexcept
        {
            if(!held() || !vr::valid_hand(opener) || !authorized || !std::isfinite(units) || units<=0)
            {cancel_pull();return false;}
            const auto delta=hands::sub(local,pull_start);
            for(float x:delta)if(!std::isfinite(x)){cancel_pull();return false;}
            // Measured against the moving tube: arm swing or moving both hands
            // together cannot detach a cap. Sideways/teleport motion cancels.
            if(hands::length(delta)>units*.35f || std::hypot(delta[0],delta[1])>units*.10f)
            {cancel_pull();return false;}
            travel=std::clamp(delta[2],0.f,units*.04f);
            return delta[2]>=units*.04f;
        }
        bool ignite(bool accepted)noexcept
        {
            if(!accepted || !held() || !vr::valid_hand(opener) || lit)return false;
            lit=cap_removed=true;cancel_pull();++revision;return true;
        }
        bool handoff(hand actor)noexcept
        {
            if(!held() || vr::valid_hand(opener) || !vr::valid_hand(actor) || actor==holder)return false;
            holder=actor;++revision;return true;
        }
        void release(bool deliberate)noexcept
        {
            if(!held())return;
            // A lit item never returns to the belt or replenishes a fresh flare.
            stage=lit?(deliberate?phase::dropped:phase::spent):phase::stowed;
            holder=hand::none;cancel_pull();++revision;
        }
        void reset(bool consumed=false)noexcept
        {const auto next=revision+1;*this={};revision=next;stage=consumed?phase::spent:phase::stowed;}
    };
    inline bool burning(const state& value,int now,int began)noexcept
    {return value.lit && (value.held() || value.stage==phase::dropped) && now>=began && std::int64_t(now)-began<burn_msec;}
    inline hands::anchor stowed_slot(const head_pose_bridge::spatial_frame& body,float half_height)noexcept
    {
        const auto chest=locate_chest(body);
        if(!chest.valid || !std::isfinite(half_height) || half_height<0)return {};
        auto at=abdomen(body);const auto frame=head_pose_bridge::body_slots_frame(body);
        const auto& up=frame.head_yaw_axis[2];
        // Keep the complete flare below the stowed knife's blade, with 3 cm
        // clearance. Native mesh dimensions stay in game units; body spacing
        // stays in metres, so changing world scale does not reintroduce overlap.
        const auto knife=knife_profile::stowed(chest);
        const auto tip=hands::pose_math::compose(knife,{knife_profile::blade_tip,{0,0,0,1}}).position;
        const float lower=std::min(0.f,hands::dot(hands::sub(tip,at.position),up)-half_height-frame.units_per_meter*.03f);
        at.position=hands::add(at.position,hands::scale(up,lower));
        // Model +Z is the cylinder; lie across the abdomen, cap to player left.
        at.rotation=hands::from_axis({frame.head_yaw_axis[0],hands::scale(frame.head_yaw_axis[2],-1),frame.head_yaw_axis[1]});
        return at;
    }
    inline hands::anchor stowed(const head_pose_bridge::spatial_frame& body,hands::vec center,float half_height)noexcept
    {
        auto at=stowed_slot(body,half_height);
        at.position=hands::sub(at.position,hands::rotate(at.rotation,center));return at;
    }
}
