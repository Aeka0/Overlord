#pragma once
#include "component/vr/gameplay/campaign/cliffhanger/policy.hpp"
#include "component/vr/gameplay/campaign/cliffhanger/profile.hpp"
#include "component/vr/gameplay/hand_interaction/core.hpp"
#include "component/vr/gameplay/hand_interaction/pose_plan.hpp"
#include "component/vr/gameplay/campaign/cliffhanger/pickaxe_policy.hpp"

template<class Check>void cliffhanger_tests(Check& check)
{
    namespace c=vr::gameplay::equipment::special::cliffhanger;
    namespace hi=vr::gameplay::hand_interaction;
    using vr::hand;
    {
        c::waist_pickaxes picks;
        check(picks.lease(0,true)!=picks.lease(1,true),"both waist picks have distinct leases before simultaneous acquisition");
        check(picks.take(0,hand::right) && picks.take(1,hand::left),"either hand can cross-draw a pick without changing its home slot");
        check(!picks.take(0,hand::left) && !picks.take(2,hand::left),"held and invalid pick slots cannot be duplicated");
        const auto right=picks.lease(1),left=picks.lease(0);
        check(picks.stow(0) && picks.holders[0]==hand::none && picks.holders[1]==hand::left && picks.lease(1)==right,
            "releasing one pick returns only that tool to its own waist");
        check(!picks.take(0,hand::left) && picks.take(0,hand::right) && picks.lease(0)!=left,"one hand cannot own both picks and regripping replaces swing history");
        picks.reset();check(picks.held(hand::left)<0 && picks.held(hand::right)<0 && picks.lease(1)!=right,
            "mode exit, checkpoint and tracking reset retire both hand leases");
        check(c::waist_picks_allowed(true,false,false,false) && !c::waist_picks_allowed(false,false,false,false) &&
            !c::waist_picks_allowed(true,true,false,false) && !c::waist_picks_allowed(true,false,true,false) &&
            !c::waist_picks_allowed(true,false,false,true),"effective cheat, story body, climbing tools and inventory transition have separate authority");
        hi::arbiter a;a.begin(1,1);
        const hi::grasp first{{hi::domain::special,{80,picks.lease(0)},40},hi::role::control,hi::button::grip,hi::recipe::single,hi::capability::melee};
        auto second=first;second.destination.object.generation=picks.lease(1);second.destination.component=41;
        check(a.observe(hand::left,first) && a.observe(hand::right,second),"independent waist tools coexist in the shared arbiter");
        check(!a.observe(hand::right,first),"two hands cannot claim the same pickaxe instance");
        check(hi::compose_pose(hand::left,a.sessions()).melee,"explicit melee equipment admits the pickaxe swing");
        hi::arbiter c4;c4.begin(1,1);auto detonator=first;detonator.abilities=hi::capability::action;
        c4.observe(hand::left,detonator);check(!hi::compose_pose(hand::left,c4.sessions()).melee,"ordinary mission equipment still suppresses fist/melee attacks");
        using namespace vr::gameplay::hands;
        vr::head_pose_bridge::spatial_frame body;body.head_position={100,200,300};body.units_per_meter=40;
        body.head_yaw_axis={vec{1,0,0},vec{0,1,0},vec{0,0,1}};
        const auto slots=vr::gameplay::weapons::carry::locate_holsters(body);
        for(unsigned side=0;side<2;++side)
        {
            const auto pose=c::pickaxe_stowed(body,slots,side);
            check(pose.position==slots.centers[side] && rotate(pose.rotation,{0,0,1})[2]<-.99f,"pick handles share configured waist anchors and hang downward");
            const auto& head=c::pickaxe_head(side);
            check(head.back()[0]>7.7f && head.back()[2]<10.1f,"sweep reaches the visible curved spike rather than the upper FX tag");
            for(const auto& v:head)check(v[2]>9.f && v[2]<16.f,"pick attacks cover the metal head without arming the long handle");
        }
    }
    check(!c::can_detonate(false,true,true,false),"installed C4 without the story cue never authorizes detonation");
    check(!c::can_detonate(true,false,true,false),"story visibility before native C4 selection is not readiness");
    check(c::can_detonate(true,true,true,false),"native story cue and selected enabled C4 authorize the prop");
    check(!c::can_detonate(true,true,false,false) && !c::can_detonate(true,true,true,true),"native weapon suppression and consumed missions reject detonation");
    check(c::protects_selection(true,35,0) && c::protects_selection(true,35,42),"holstering or drawing a gun cannot replace the active native C4 selection");
    check(!c::protects_selection(false,35,0) && !c::protects_selection(true,35,35),"normal carry and the actual C4 selection remain permitted");
    for(auto h:{hand::left,hand::right})
    {
        c::trigger_gate gate;c::context mission;mission.active=true;mission.generation=1;
        vr::controller_input::frame f;f.reference_generation=1;f.continuity_generation=1;f.sampled_at=vr::controller_input::clock::now();
        auto& input=f.trigger[unsigned(h)];input.active=true;input.generation=1;input.down=true;input.presses=1;
        check(!gate.consume(f,mission,h,1,true),"early held trigger cannot activate the prop");
        mission.authorized=true;
        check(!gate.consume(f,mission,h,1,true),"holding trigger across the story cue is not a fresh press");
        check(!gate.consume(f,mission,h,1,false),"native weapon raise or busy state rejects trigger input");
        check(!gate.consume(f,mission,h,1,true),"holding trigger through native raise cannot become a buffered detonation");
        input.down=false;++input.releases;check(!gate.consume(f,mission,h,1,true),"neutral sample arms without firing");
        input.down=true;++input.presses;check(gate.consume(f,mission,h,1,true),"new owning-hand trigger reaches the native input gate");
        mission.native_body=true;check(!gate.consume(f,mission,h,1,true),"native body ownership cannot be bypassed by a stale ready flag");
        mission.native_body=false;check(!gate.consume(f,mission,h,1,true),"returning from a scripted body does not replay held trigger");
        input.down=false;gate.consume(f,mission,h,1,true);input.down=true;++input.presses;
        ++mission.generation;check(!gate.consume(f,mission,h,1,true),"checkpoint replacement cancels old trigger intent");
        input.down=false;gate.consume(f,mission,h,1,true);input.down=true;++input.presses;
        ++f.continuity_generation;check(!gate.consume(f,mission,h,1,true),"tracking continuity replacement cancels old trigger intent");
        input.down=false;gate.consume(f,mission,h,1,true);input.down=true;++input.presses;
        f.sampled_at+=std::chrono::seconds(1);check(!gate.consume(f,mission,h,1,true),"a long pause requires another release before activation");
        mission.consumed=true;check(!gate.consume(f,mission,h,1,true),"confirmed detonation cannot repeat");
    }
    check(c::opening_start("default") && c::opening_start("jump") && !c::opening_start("clifftop"),"direct late checkpoints cannot grant opening ice picks");
    check(c::pick_definition("ice_picker") && c::pick_definition("ice_picker_bigjump") && !c::pick_definition("c4"),"temporary pick exclusion is an exact native definition identity");
    hi::arbiter arbiter;arbiter.begin(1,1);
    const hi::grasp left{{hi::domain::special,{35,9},31},hi::role::control,hi::button::none,hi::recipe::single,hi::capability::action};
    auto right=left;right.destination.component=32;
    check(arbiter.observe(hand::left,left) && arbiter.observe(hand::right,right),"two story picks have independent one-hand owners without a dual grip");
    arbiter.begin(2,1);check(arbiter.find(hand::left,left.destination) && arbiter.find(hand::right,right.destination),"pick ownership does not depend on maintained grip input");
    check(c::authored::pick_l_attachment.position!=c::authored::pick_r_attachment.position,"left and right picks retain their separately captured wrist frames");
    for(const auto& frame:c::authored::detonator_fire)for(const auto& bone:frame)
    {
        float norm{};for(float q:bone.rotation)norm+=q*q;
        check(std::isfinite(norm) && std::abs(norm-1)<.001f,"all authored mechanical animation rotations remain finite and normalized");
    }
}
