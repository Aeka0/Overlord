#pragma once
#include "component/vr/digital_button_gate.hpp"
#include "free_climb.hpp"
#include "../controller_input.hpp"
#include <string_view>

namespace vr::gameplay::ending
{
    enum class stage { none, wakeup, approach, subdual, grounded, crawl, kick, wounded, pull_wait, pull, throw_ready, throwing, rescue, failed };
    struct scene_evidence
    {
        std::string_view mode,start;
        bool ready{},credits{},standing{},subdual{},impaled{},bloody{},crawling{},crawled{},wounded{},using_knife{},pulled{},throw_ready{},thrown{},killed{},grasp_enabled{},failed{};
    };
    inline bool known_entry(std::string_view n) noexcept
    {
        for(const auto value:{"wakeup","wakefast","default","turnbuckle","gun_fight","crawl","gun_kick","wounded","pullout","kill","endgame"})if(n==value)return true;
        return false;
    }
    inline stage classify(const scene_evidence& e) noexcept
    {
        if(e.mode!="credits_1" || !e.ready || e.credits || !known_entry(e.start))return stage::none;
        if(e.failed)return stage::failed;
        if(e.thrown || e.killed || e.start=="endgame")return stage::rescue;
        if(e.throw_ready)return stage::throw_ready;
        if(e.pulled || e.start=="kill")return stage::throwing;
        if(e.using_knife)return stage::pull;
        if(e.wounded || e.start=="wounded" || e.start=="pullout")return e.grasp_enabled?stage::pull_wait:stage::wounded;
        if(e.crawled || e.start=="gun_kick")return stage::kick;
        if(e.crawling || e.start=="crawl")return stage::crawl;
        if(e.impaled || e.bloody || e.start=="gun_fight")return stage::grounded;
        if(e.subdual)return stage::subdual;
        return !e.standing && e.start!="turnbuckle" ? stage::wakeup:stage::approach;
    }
    inline bool independent(stage s) noexcept {return s==stage::grounded || s==stage::crawl || s==stage::kick;}
    inline bool body_control(stage s) noexcept {return s==stage::wounded || s==stage::pull_wait || s==stage::pull || s==stage::throw_ready;}
    // The injured body is already controller-driven during the fight spectator
    // interval; both anatomical poles are needed before the knife grip opens.
    inline unsigned outward_elbow_hands(stage s) noexcept
    {return s==stage::wounded || s==stage::pull_wait || s==stage::pull ? 3u:0u;}
    inline constexpr float crawl_eye_height_meters=.35f;
    inline constexpr int crawl_settle_milliseconds=650;
    inline float crawl_height_blend(float from,float to,int elapsed) noexcept
    {
        const float t=std::clamp(float(elapsed)/crawl_settle_milliseconds,0.f,1.f);
        return from+(to-from)*t*t*(3-2*t);
    }
    inline constexpr float throw_gaze_dot=.819152f; // 35 degrees; shared by gaze and gesture.
    inline bool museum_camera(std::string_view mode,bool transitioned,bool linked_to_camera) noexcept
    {return linked_to_camera && (mode=="credits_2" || (mode=="credits_1" && transitioned));}
    struct hand_sample {hands::anchor wrist{};bool valid{};};
    struct crawl_arrival {unsigned hands{};float nearest_meters{-1};};
    // Arrival uses either valid tracked wrist. All positions share the native
    // world frame; neither HMD proximity nor a missing controller can trigger it.
    inline crawl_arrival crawl_reach(const std::array<hand_sample,2>& wrists,hands::vec revolver,float units) noexcept
    {
        crawl_arrival out;
        if(!std::isfinite(units) || units<=0 || !free_climb::finite(revolver))return out;
        const float radius=.4f*units;
        for(unsigned h=0;h<2;++h)if(wrists[h].valid && free_climb::finite(wrists[h].wrist.position))
        {
            const auto d=hands::sub(wrists[h].wrist.position,revolver);
            const float distance=hands::length(d)/units;
            if(out.nearest_meters<0 || distance<out.nearest_meters)out.nearest_meters=distance;
            if(hands::dot(d,d)<=radius*radius)out.hands|=1u<<h;
        }
        return out;
    }
    inline const char* name(stage s) noexcept
    {
        switch(s){case stage::wakeup:return "wakeup";case stage::approach:return "approach";case stage::subdual:return "subdual";
        case stage::grounded:return "grounded";case stage::crawl:return "crawl";case stage::kick:return "kick";case stage::wounded:return "wounded";
        case stage::pull_wait:return "pull_wait";case stage::pull:return "pull";case stage::throw_ready:return "throw_ready";
        case stage::throwing:return "throwing";case stage::rescue:return "rescue";case stage::failed:return "failed";default:return "none";}
    }
    // Raw physical wrist motion drives work; rendered wrists stay at the knife.
    // A held button cannot acquire after tracking loss, recenter or stage change.
    class pull_gesture
    {
        std::array<controller_input::digital_press_gate,2> edges_{};
        std::array<hands::quat,2> previous_{};
        unsigned held_{},sampled_{};
        std::uint64_t sequence_{},reference_{},continuity_{};
        controller_input::clock::time_point at_{};
    public:
        void reset() noexcept {*this={};}
        unsigned held() const noexcept {return held_;}
        float update(const controller_input::frame& input,const std::array<hand_sample,2>& hands,
            const std::array<vr::gameplay::hands::anchor,2>& grips,float units,bool admitted) noexcept
        {
            using namespace vr::gameplay::hands;
            if(!admitted || !input.sequence || !input.reference_generation || !input.focused || input.orientation_settling ||
                !std::isfinite(units) || units<=0){reset();return 0;}
            if(reference_!=input.reference_generation || continuity_!=input.continuity_generation || input.sequence<sequence_ ||
                input.sampled_at<at_ || (sequence_ && input.sampled_at-at_>std::chrono::milliseconds(150)))reset();
            if(input.sequence==sequence_)return 0;
            const float dt=sequence_?std::chrono::duration<float>(input.sampled_at-at_).count():0;
            reference_=input.reference_generation;continuity_=input.continuity_generation;sequence_=input.sequence;at_=input.sampled_at;
            float work{};
            for(unsigned h=0;h<2;++h)
            {
                const auto bit=1u<<h;const auto& b=input.squeeze[h];const auto& hand=hands[h];
                const bool pressed=edges_[h].consume(b);
                if(!hand.valid || !free_climb::finite(hand.wrist.position) || !free_climb::rotation_valid(hand.wrist.rotation) || !b.active || !b.down)
                {held_&=~bit;sampled_&=~bit;if(!hand.valid)edges_[h]={};continue;}
                if(!(held_&bit) && pressed && free_climb::finite(grips[h].position) && length(sub(hand.wrist.position,grips[h].position))<=.10f*units)
                {held_|=bit;sampled_&=~bit;}
                const auto q=normalize(hand.wrist.rotation);
                if((held_&bit) && (sampled_&bit) && dt>=.001f && dt<=.15f)
                {
                    float dot{};for(unsigned i=0;i<4;++i)dot+=previous_[h][i]*q[i];
                    const float angle=2*std::acos(std::clamp(std::abs(dot),0.f,1.f)),speed=angle/dt;
                    // Suppress sensor noise and orientation discontinuities. Two
                    // hands stabilize the grip, but do not double extraction speed.
                    if(speed>=.6f && speed<=18.f && angle<=.8f)work=std::max(work,angle/5.f);
                }
                previous_[h]=q;if(held_&bit)sampled_|=bit;
            }
            return work;
        }
    };
    class throw_gesture
    {
        bool armed_{},done_{};hands::vec baseline_{},relative_baseline_{};
        std::uint64_t reference_{};int began_{};
    public:
        void reset() noexcept {*this={};}
        bool update(hands::vec hand_position,hands::vec head_position,hands::vec toward_target,float gaze_dot,std::uint64_t reference,int time,bool valid) noexcept
        {
            using namespace hands;
            if(!valid || !reference || !free_climb::finite(hand_position) || !free_climb::finite(head_position) || !free_climb::finite(toward_target) ||
                !std::isfinite(gaze_dot) || gaze_dot<throw_gaze_dot || length(toward_target)<.9f){reset();return false;}
            if(reference_!=reference || time<began_)reset();reference_=reference;
            if(done_)return false;
            const auto hand_from_head=sub(hand_position,head_position);
            if(!armed_)
            {
                // Restore the deliberate preparation gate now that the native
                // held prop no longer blocks admission to the interaction.
                if(dot(hand_from_head,toward_target)<-.02f)return false;
                armed_=true;baseline_=hand_position;relative_baseline_=hand_from_head;began_=time;return false;
            }
            const auto displacement=sub(hand_position,baseline_),relative=sub(hand_from_head,relative_baseline_);
            if(length(displacement)>.65f){reset();return false;}
            // Require both physical controller travel and arm travel relative
            // to the head: leaning forward or moving the whole body cannot throw.
            if(time-began_>=120 && dot(displacement,toward_target)<=-.14f && dot(relative,toward_target)<=-.14f)
            {done_=true;return true;}
            return false;
        }
    };
}
