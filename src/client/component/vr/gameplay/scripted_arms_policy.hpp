#pragma once
#include "native_animation_query.hpp"
#include "weapon_profile.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

namespace vr::gameplay::scripted_arms
{
    enum class model_profile { none, arctic, ending_wet, ending_injured };
    enum class control { authored, tracked };
    // Body ownership is independent of camera ownership and native input locks.
    // Future scenes can select either/both arms and a body other than the camera rig.
    struct request
    {
        model_profile profile{};
        control mode{};
        int entity{-1};
        unsigned hands{3};
        std::uint64_t reference{};
        unsigned locked{},grasp_hands{};
        unsigned outward_elbows{}; // Optional anatomical outward/down pole per arm.
        std::array<vr::gameplay::hands::anchor,2> grips{}; // World poses; only the locked hands consume them.
        const weapons::profile* hand_pose{}; // Immutable preset, applied only to grasp_hands.
        // Optional native DObj prop, attached by the scene script. Pose its
        // bone in this SAME skeleton after wrist IK; no separate render record.
        const char* attached_model{};
        const char* attached_bone{};
        unsigned attached_hand{1};
        vr::gameplay::hands::anchor attached_in_wrist{};
        bool reserved() const noexcept
        {return profile!=model_profile::none && entity>0 && entity<4000 && hands && !(hands&~3u);}
    };
    struct profile
    {
        std::string_view model,torso;
        std::span<const std::string_view> rest;
        std::string_view takeover;
        std::span<const std::string_view> automatic;
    };
    inline const profile* find_profile(model_profile id) noexcept
    {
        static constexpr std::array<std::string_view,2> rest{
            "h2_cliffhanger_player_intro","h2_cliffhanger_player_idle"};
        static constexpr std::array<std::string_view,6> automatic{
            "h2_cliffhanger_ledgewalking_getready",
            "h2_cliffhanger_iceaxeclimbing_getready_2_climb_right",
            "h2_cliffhanger_iceaxeclimbing_getready_2_climb_left",
            "h2_cliffhanger_iceaxeclimbing_alternate_start_right",
            "h2_cliffhanger_iceaxeclimbing_alternate_start_left",
            "h2_playerview_icepicker_bigjump_left_01"};
        static const profile arctic{"viewbody_arctic","tag_torso",rest,"h2_cliffhanger_ledgewalking_in",automatic};
        static constexpr std::array<std::string_view,5> ending_automatic{
            "h2_afchase_player_knife_pullout_2_flip","h2_afchase_player_knife_throw",
            "h2_afchase_player_knife_throw_kill","h2_afchase_player_throw_passout","h2_afchase_player_endgame"};
        static const profile wet{"viewbody_tf141_wet","tag_torso",{},"",ending_automatic};
        static const profile injured{"viewbody_tf141_injured","tag_torso",{},"",ending_automatic};
        return id==model_profile::arctic ? &arctic : id==model_profile::ending_wet ? &wet : id==model_profile::ending_injured ? &injured : nullptr;
    }
    inline float smooth(float t) noexcept
    {t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t);}
    // An authored stand-up, including its duration/rate/pause, is the transition
    // clock. Never infer completion from ledge_started or an elapsed wall timer.
    inline float animation_weight(const profile& p,std::span<const native_animation::clip> clips) noexcept
    {
        if(clips.empty())return 0;
        bool resting{},rising{};float progress=1;
        for(const auto& c:clips)
        {
            if(!std::isfinite(c.weight) || c.weight<0)return 0;
            if(c.weight<=0)continue;
            // Native automatic actions own both arms even if scene flags or a
            // previously published tracked state have not caught up this frame.
            if(std::find(p.automatic.begin(),p.automatic.end(),c.name)!=p.automatic.end())return 0;
            resting|=std::find(p.rest.begin(),p.rest.end(),c.name)!=p.rest.end();
            if(c.name==p.takeover)
            {
                if(!std::isfinite(c.time) || c.time<0 || c.time>1.001f)return 0;
                rising=true;progress=std::min(progress,c.time);
            }
        }
        return rising ? smooth(progress) : resting ? 0.f : 1.f;
    }
    // A new body, restored reference or recovered controller starts from the
    // current native pose. The normal stand-up is slower than this recovery
    // envelope, so its authored progress remains the limiting weight.
    class transition
    {
    public:
        void reset() noexcept {*this={};}
        std::array<float,2> update(std::uint64_t reference,int time,unsigned tracked,float authored) noexcept
        {
            if(!reference || !std::isfinite(authored) || authored<0 || authored>1){reset();return {};}
            if(reference!=reference_ || time<time_ || std::int64_t(time)-time_>250)valid_.fill(false);
            reference_=reference;time_=time;std::array<float,2> out{};
            for(unsigned h=0;h<2;++h)
            {
                if(!(tracked&(1u<<h)) || authored==0){valid_[h]=false;continue;}
                if(!valid_[h]){began_[h]=time;valid_[h]=true;}
                out[h]=std::min(authored,smooth(float(std::int64_t(time)-began_[h])/200.f));
            }
            return out;
        }
    private:
        std::uint64_t reference_{};
        int time_{};
        std::array<int,2> began_{};
        std::array<bool,2> valid_{};
    };
    struct pose_epoch
    {
        std::uintptr_t object{},matrices{};
        std::uint32_t timestamp{};
        bool operator==(const pose_epoch&) const = default;
    };
    class once_per_pose
    {
    public:
        bool begin(pose_epoch key,bool rebuilt) noexcept
        {
            if(!key.object || !key.matrices || (!rebuilt && key==completed_))return false;
            completed_=key;return true;
        }
    private:
        pose_epoch completed_{};
    };
}
