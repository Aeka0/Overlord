#pragma once
#include "../../special_equipment_policy.hpp"
#include "component/vr/digital_button_gate.hpp"
#include "level_rules.hpp"

namespace vr::gameplay::equipment::special::cliffhanger
{
    struct context
    {
        bool active{},opening{},native_body{},picks{},detonator{},authorized{},consumed{},native_c4{};
        unsigned weapon{};
        std::uint64_t generation{};
        int time{},detonated_at{-1};
        bool oilrig{},owns_c4{}; // Oilrig shares the native C4 model, grasp and Trigger path.
        bool waist_picks{}; // Green Beret tools; never the native climbing inventory.
    };
    using ::vr::gameplay::cliffhanger::opening_start;
    inline bool pick_definition(std::string_view name) noexcept
    {return name=="ice_picker" || name=="ice_picker_bigjump";}
    // Installation flags deliberately do not participate in authorization.
    inline bool can_detonate(bool story_cue,bool selected_c4,bool weapons_enabled,bool consumed) noexcept
    {return story_cue && selected_c4 && weapons_enabled && !consumed;}
    inline bool protects_selection(bool vr_story_session,unsigned c4,unsigned requested)noexcept
    {return vr_story_session && c4>0 && c4<512 && requested!=c4;}
    struct trigger_gate
    {
        controller_input::digital_button_gate edge;
        std::uint64_t epoch{},reference{},revision{},continuity{};
        controller_input::clock::time_point sampled{};
        vr::hand holder{vr::hand::none};
        bool ready{};
        void reset()noexcept{*this={};}
        bool consume(const controller_input::frame& input,const context& mission,vr::hand hand,
            std::uint64_t ownership,bool allowed) noexcept
        {
            const bool usable=allowed && mission.authorized && !mission.native_body && !mission.consumed && vr::valid_hand(hand);
            if(!usable){reset();return false;}
            if(!ready || epoch!=mission.generation || reference!=input.reference_generation ||
                revision!=ownership || holder!=hand || continuity!=input.continuity_generation || input.sampled_at<sampled ||
                input.sampled_at-sampled>std::chrono::milliseconds(150))edge={};
            ready=true;epoch=mission.generation;reference=input.reference_generation;revision=ownership;holder=hand;
            continuity=input.continuity_generation;sampled=input.sampled_at;
            return edge.consume(input.trigger[unsigned(hand)]);
        }
    };
    inline hands::anchor detonator_stowed(const head_pose_bridge::spatial_frame& body,hands::vec center) noexcept
    {
        auto result=abdomen(body);
        const auto frame=head_pose_bridge::body_slots_frame(body);
        result.rotation=hands::from_axis({frame.head_yaw_axis[1],hands::scale(frame.head_yaw_axis[0],-1),frame.head_yaw_axis[2]});
        result.position=hands::sub(result.position,hands::rotate(result.rotation,center));return result;
    }
}
