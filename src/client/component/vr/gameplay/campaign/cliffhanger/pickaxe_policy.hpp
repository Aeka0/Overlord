#pragma once
#include "../../weapon_holsters.hpp"
#include "../../hand_interaction/core.hpp"

namespace vr::gameplay::equipment::special::cliffhanger
{
    // Two body tools, each permanently assigned to its original waist slot.
    // Leases are side-distinct even when both native weapons share one token.
    struct waist_pickaxes
    {
        std::array<vr::hand,2> holders{vr::hand::none,vr::hand::none};
        std::array<std::uint64_t,2> revisions{1,1};
        int held(vr::hand hand) const noexcept
        {
            if(!vr::valid_hand(hand))return -1;
            for(unsigned i=0;i<2;++i)if(holders[i]==hand)return int(i);
            return -1;
        }
        std::uint64_t lease(unsigned side,bool next=false) const noexcept
        {return (revisions[side]+unsigned(next))*2+side;}
        bool take(unsigned side,vr::hand hand) noexcept
        {
            if(side>=2 || !vr::valid_hand(hand) || vr::valid_hand(holders[side]) || held(hand)>=0)return false;
            holders[side]=hand;++revisions[side];return true;
        }
        bool stow(unsigned side) noexcept
        {
            if(side>=2 || !vr::valid_hand(holders[side]))return false;
            holders[side]=vr::hand::none;++revisions[side];return true;
        }
        void reset() noexcept {for(unsigned i=0;i<2;++i){holders[i]=vr::hand::none;++revisions[i];}}
    };
    inline bool waist_picks_allowed(bool green,bool native_body,bool story_picks,bool transition) noexcept
    {return green && !native_body && !story_picks && !transition;}
    inline hands::anchor pickaxe_stowed(const head_pose_bridge::spatial_frame& frame,
        const weapons::carry::holsters& slots,unsigned side) noexcept
    {
        const auto body=head_pose_bridge::body_slots_frame(frame);
        // Keep the handle above the head (+Z down), with the +X spike facing rearward.
        return {slots.centers[side],hands::from_axis({hands::scale(body.head_yaw_axis[0],-1),
            body.head_yaw_axis[1],hands::scale(body.head_yaw_axis[2],-1)})};
    }
    // Native LOD0 head cross-sections (2026-09-29 capture), in j_gun inches.
    // The FX tag at (2.02,0,14.82) is not the visible spike at (7.75,0,9.99).
    // Cover the metal head and curved spike, not the handle or an inflated sphere.
    inline constexpr std::array<hands::vec,9> pickaxe_head_left{{
        {.231f,0,14.861f},{1.522f,0,14.856f},{2.504f,0,14.431f},
        {3.499f,0,14.168f},{4.494f,0,13.296f},{5.499f,0,12.084f},
        {6.504f,0,11.081f},{7.633f,0,10.238f},{7.752307f,0,9.985391f}}};
    inline constexpr std::array<hands::vec,9> pickaxe_head_right{{
        {-1.780385f,0,14.750525f},{-.018f,0,14.925f},{1.494f,0,14.844f},
        {2.504f,0,14.431f},{3.499f,0,14.168f},{4.494f,0,13.296f},
        {5.499f,0,12.084f},{6.504f,0,11.081f},{7.752307f,0,9.985391f}}};
    inline const std::array<hands::vec,9>& pickaxe_head(unsigned side) noexcept
    {return side?pickaxe_head_right:pickaxe_head_left;}
}
