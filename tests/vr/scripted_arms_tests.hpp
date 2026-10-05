#pragma once
#include "component/vr/gameplay/hand_rig_builder.hpp"
#include "component/vr/gameplay/scripted_arms_pose.hpp"
#include "component/vr/gameplay/scripted_arms_policy.hpp"
#include <cstring>
#include <limits>

namespace scripted_arms_tests
{
    template<class Check>void run(Check check)
    {
        using namespace vr::gameplay;
        using namespace hands;
        using namespace scripted_arms;
        using native_animation::clip;
        const auto& profile=*find_profile(model_profile::arctic);
        const auto weight=[&](std::initializer_list<clip> clips){return animation_weight(profile,{clips.begin(),clips.size()});};
        check(weight({{"h2_cliffhanger_player_intro",1,1}})==0 && weight({{"h2_cliffhanger_player_idle",.7f,1}})==0,
            "intro and crouched rest never follow controllers even if a story flag arrives early");
        check(weight({{profile.takeover,0,1}})==0 && weight({{profile.takeover,.5f,1}})==.5f && weight({{profile.takeover,1,1}})==1,
            "stand-up takeover follows actual native animation progress");
        check(weight({{profile.rest[1],.8f,.8f},{profile.takeover,.5f,.2f}})==.5f,
            "a native animation crossfade does not postpone takeover until after standing");
        check(weight({{"h2_cliffhanger_ledgewalking_in_idle",.1f,1}})==1,
            "standing checkpoints and later body animations admit tracking");
        for(const auto name:profile.automatic)
            check(weight({{name,.5f,1}})==0 && weight({{profile.takeover,1,.5f},{name,.5f,.5f}})==0,
                "automatic pick preparation and native entry stabs cannot be overridden by controllers");
        check(weight({{"h2_cliffhanger_iceaxeclimbing_getready_2_climb_idle_right",0,1}})==1 &&
            weight({{"h2_playerview_icepicker_bigjump_left_01_idle",0,1}})==1,
            "both native input-loop idle poses admit tracked hands after entry ownership releases");
        check(weight({})==0 && weight({{profile.takeover,std::numeric_limits<float>::quiet_NaN(),1}})==0,
            "missing or malformed native animation progress retains authored arms");
        transition blend;
        check(blend.update(1,1000,3,.3f)==std::array<float,2>{0,0},"new body starts exactly at native pose");
        check(blend.update(1,1100,3,.3f)==std::array<float,2>{.3f,.3f},"slow get-up weight caps recovery envelope");
        check(blend.update(1,1200,3,.4f)==std::array<float,2>{.4f,.4f},"stand-up does not finish after a fixed timer");
        check(blend.update(1,1200,3,.4f)==std::array<float,2>{.4f,.4f},"paused game/animation time cannot advance blend");
        check(blend.update(1,1300,1,.6f)==std::array<float,2>{.6f,0},"tracking loss is independent per arm");
        check(blend.update(1,1400,3,.8f)==std::array<float,2>{.8f,0},"recovered hand starts at current native pose");
        check(blend.update(2,1500,3,1)==std::array<float,2>{0,0},"reference replacement blends again");
        check(blend.update(2,1450,3,1)==std::array<float,2>{0,0},"checkpoint rewind resets takeover");
        once_per_pose epoch;
        check(epoch.begin({1,2,3},true) && !epoch.begin({1,2,3},false),"stereo and partial queries cannot solve a body twice");
        check(epoch.begin({1,2,4},true) && !epoch.begin({1,2,4},false),"tracking recovery cannot retry a latched rejection in one skeleton epoch");
        check(epoch.begin({1,2,4},true) && epoch.begin({1,5,4},false),"native rebuild and new matrix storage each admit one pose");

        // Semantic topology witnessed on viewbody_arctic, 2026-09-30. Reduced
        // fixture has synthetic transforms and wrist-attached twin prop models;
        // no mesh or animation asset is redistributed.
        constexpr int count=25;
        std::array<bone_definition,count> defs{{
            {"tag_origin",-1},{"j_mainroot",0},{"tag_view",0},{"tag_camera",2},{"tag_player",3},
            {"j_hip_le",1},{"j_spinelower",1},{"tag_torso",2},{"j_clavicle_le",7},{"j_clavicle_ri",7},{"tag_weapon",7},
            {"j_shoulder_le",8},{"j_shoulder_ri",9},{"j_elbow_le",11},{"j_elbow_ri",12},
            {"j_wrist_le",13},{"j_wrist_ri",14},{"j_index_le_0",15},{"j_index_ri_0",16},
            {"tag_weapon_left",15},{"tag_weapon_right",16},
            {"j_gun",19},{"tag_tip",21},{"j_gun",20},{"tag_tip",23}}};
        std::array<bone,count> native{};
        for(int b=0;b<count;++b)native[b]={{0,0,0,1},{float(b)*.2f,0,30},2};
        native[11].position={0,8,30};native[12].position={0,-8,30};
        native[13].position={6,11,25};native[14].position={6,-11,25};
        native[15].position={12,8,23};native[16].position={12,-8,23};
        for(int b=17;b<count;++b)native[b].position=add(native[defs[b].parent].position,{1,0,0});
        for(int b=0;b<count;++b)defs[b].bind=native[b];
        const std::array<model_definition,3> models{{{"viewbody_arctic",0,21},{"left_pick",21,2},{"right_pick",23,2}}};
        auto resolution=resolve_rig(models,defs,rig_kind::scripted_body);
        check(!resolution.rejection && resolution.layout.gun==-1,"body rig admits independent wrist props without firearm/muzzle contract");
        check(resolve_rig(models,defs,rig_kind::firearm).rejection && resolve_rig(models,defs,rig_kind::hands_only).rejection,
            "scripted body admission does not weaken legacy firearm or ordinary hands contracts");
        const auto saved=defs[23].parent;defs[23].parent=4;
        check(resolve_rig(models,defs,rig_kind::scripted_body).rejection,"camera-attached prop is not silently moved with arms");
        defs[23].parent=saved;
        const auto r=resolution.layout;
        std::array<std::uint32_t,8> requested{};
        requested[0]=0x80000000u>>4;
        check(!requests_arm_pose(r,requested),"camera-only tag query must not pre-complete the arm pose");
        requested[0]|=0x80000000u>>15;
        check(requests_arm_pose(r,requested),"native request for an arm admits the scripted arm pass");
        const std::array<anchor,2> targets{{{{5,14,37},{0,0,.70710678f,.70710678f}},{{7,-10,36},{0,.38268343f,0,.92387953f}}}};
        const std::array<vec,2> shoulders{native[11].position,native[12].position};
        const std::array<vec,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
        std::array<bone,count> solved{},out{},small{};std::array<bool,2> limited{};
        check(solve_arms(r,native,targets,shoulders,axes,solved,limited),"full body uses shared anatomical arm solver");
        check(blend_pose(r,native,solved,{0,0},out) && std::memcmp(out.data(),native.data(),sizeof(native))==0,
            "zero takeover weight preserves the entire authored skeleton byte-for-byte");
        check(blend_pose(r,native,solved,{.5f,1},out),"mixed left/right controller weights are supported");
        for(int b=0;b<count;++b)
        {
            if(arm_owner(r,b)<0)check(std::memcmp(&out[b],&native[b],sizeof(bone))==0,"root, legs, torso and camera bones remain byte-exact");
            if(arm_owner(r,b)==1)check(std::memcmp(&out[b],&solved[b],sizeof(bone))==0,"full weight reaches exact solved right arm");
        }
        check(out[11].position==native[11].position && out[12].position==native[12].position,"native shoulder positions remain fixed");
        {
            auto chest_targets=targets;chest_targets[0].position={7,1,27};
            std::array<bone,count> normal{},outward{};
            const vec left_hint=add(shoulders[0],{0,8,-2.8f});
            check(solve_arms(r,native,chest_targets,shoulders,axes,normal,limited,1.f) &&
                solve_arms(r,native,chest_targets,shoulders,axes,outward,limited,1.f,{&left_hint,nullptr}),
                "scripted arms reuse per-hand elbow hints for chest-level work");
            check(outward[13].position[1]>normal[13].position[1] && length(sub(outward[15].position,normal[15].position))<.001f,
                "left elbow moves outward without pulling the locked wrist off the knife");
            check(outward[12].position==normal[12].position && outward[14].position==normal[14].position && outward[16].position==normal[16].position,
                "a left elbow preference cannot move the right arm");
        }
        const auto local=[&](const auto& pose,int b){return rotate(conjugate(normalize(pose[r.parent[b]].rotation)),sub(pose[b].position,pose[r.parent[b]].position));};
        for(int b:{17,18,19,20,21,22,23,24})check(length(sub(local(out,b),local(native,b)))<.0001f,
            "native fingers and independent picks retain local transforms during transition");
        check(blend_pose(r,native,solved,{.0001f,0},small) && length(sub(small[13].position,native[13].position))<.01f,
            "native elbow has no pole snap at takeover start");
        check(blend_pose(r,native,solved,{1,0},out),"single hand tracking succeeds");
        for(int b=0;b<count;++b)if(arm_owner(r,b)!=0)
            check(std::memcmp(&out[b],&native[b],sizeof(bone))==0,"uncontrolled hand and all other body bones stay authored");
        auto moving=native;for(auto& b:moving)b.position=add(b.position,{100,-20,40});
        auto moving_solved=solved;for(auto& b:moving_solved)b.position=add(b.position,{100,-20,40});
        std::array<bone,count> moving_out{};
        check(blend_pose(r,moving,moving_solved,{.5f,.5f},moving_out) && blend_pose(r,native,solved,{.5f,.5f},out),"moving body transition evaluates");
        for(int b=0;b<count;++b)check(length(sub(moving_out[b].position,add(out[b].position,{100,-20,40})))<.0001f,
            "authored root travel is preserved at full amplitude during arm blending");
        const auto before=out;auto corrupt=solved;corrupt[15].rotation[0]=std::numeric_limits<float>::quiet_NaN();
        {
            auto held=out;const anchor grip{{2,-1,0},{0,0,0,1}};
            check(pose_attached_prop(r,23,1,grip,held),"native right-wrist prop accepts an authored local grip in the same body skeleton");
            const auto expected=pose_math::compose(pose_math::as_anchor(held[16]),grip);
            check(length(sub(held[23].position,expected.position))<.0001f &&
                length(sub(local(held,24),local(out,24)))<.0001f,"knife and its child tags follow the committed wrist with no external pose record");
            for(int b=0;b<23;++b)check(std::memcmp(&held[b],&out[b],sizeof(bone))==0,"placing a held prop cannot move the hand or the story camera");
            check(!pose_attached_prop(r,4,1,grip,held) && !pose_attached_prop(r,21,1,grip,held),"camera and opposite-hand props cannot be claimed by the right-wrist attachment");
        }
        check(!blend_pose(r,native,corrupt,{1,1},out) && std::memcmp(out.data(),before.data(),sizeof(out))==0,
            "bad skeleton fails transactionally without partially writing the body");
        auto displaced=solved;displaced[r.arms[0].shoulder].position[0]+=1;
        check(!blend_pose(r,native,displaced,{1,1},out) && std::memcmp(out.data(),before.data(),sizeof(out))==0,
            "full takeover cannot move an authored shoulder even if a future solver proposes it");
        auto distant=targets;distant[0].position=add(shoulders[0],{50,0,0});
        check(solve_arms(r,native,distant,shoulders,axes,solved,limited,1.f) && limited[0],
            "unreachable scripted-body hand is reach-limited without stretching");
        const auto& arm=r.arms[0];
        check(std::abs(length(sub(solved[arm.elbow].position,solved[arm.shoulder].position))-
            length(sub(native[arm.elbow].position,native[arm.shoulder].position)))<.001f &&
            std::abs(length(sub(solved[arm.wrist].position,solved[arm.elbow].position))-
            length(sub(native[arm.wrist].position,native[arm.elbow].position)))<.001f &&
            solved[arm.shoulder].position==native[arm.shoulder].position,
            "native shoulder position and both limb lengths survive extreme controller reach");


    }
}
