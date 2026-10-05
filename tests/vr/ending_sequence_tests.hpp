#pragma once
#include "component/vr/gameplay/sequences/ending.hpp"
#include "component/vr/gameplay/ending_grips.hpp"
#include "component/vr/camera_rig.hpp"

namespace ending_sequence_tests
{
    template<class Check> void run(Check check)
    {
        namespace e=vr::gameplay::ending;
        namespace seq=vr::gameplay::sequences;
        using namespace vr::gameplay::hands;using namespace std::chrono_literals;
        e::scene_evidence state{"credits_1","wakeup",true};
        check(e::classify(state)==e::stage::wakeup,"ending wakeup owns the authored player model");
        state.standing=true;check(e::classify(state)==e::stage::approach,"standing releases hands for the playable approach");
        state.subdual=true;check(e::classify(state)==e::stage::subdual,"both native subdual branches share their start boundary");
        state.impaled=true;check(e::classify(state)==e::stage::grounded && e::independent(e::classify(state)),"knife impact switches to independent VR arms and hidden body");
        state.crawling=true;check(e::classify(state)==e::stage::crawl,"crawl input does not restore weapon carry");
        state.crawled=true;check(e::classify(state)==e::stage::kick && e::independent(e::classify(state)),"ordinary VR arms continue through the kick");
        state.wounded=true;check(e::classify(state)==e::stage::wounded && e::body_control(e::classify(state)),"post-kick knife scene uses tracked native body arms");
        state.grasp_enabled=true;check(e::classify(state)==e::stage::pull_wait,"physical grip owns the native knife-use gate");
        state.using_knife=true;check(e::classify(state)==e::stage::pull,"native extraction phase retains controller body arms");
        for(const auto stage:{e::stage::wounded,e::stage::pull_wait,e::stage::pull})
            check(e::outward_elbow_hands(stage)==3,"both elbows use anatomical outward hints throughout watching and extraction");
        for(const auto stage:{e::stage::none,e::stage::wakeup,e::stage::crawl,e::stage::kick,e::stage::throw_ready,e::stage::throwing,e::stage::rescue})
            check(!e::outward_elbow_hands(stage),"outward hints cannot escape the wounded-body interaction interval");
        state.pulled=true;check(e::classify(state)==e::stage::throwing,"native pullout flip temporarily owns its arms");
        state.throw_ready=true;check(e::classify(state)==e::stage::throw_ready,"throw preparation starts only at the native ready boundary");
        state.thrown=true;check(e::classify(state)==e::stage::rescue && !e::body_control(e::classify(state)),"accepted throw returns to authored arms through rescue");
        state.failed=true;check(e::classify(state)==e::stage::failed && !e::body_control(e::classify(state)),"mission failure revokes physical gestures and preserves the authored failure body");state.failed=false;
        state.credits=true;check(e::classify(state)==e::stage::none,"credits handoff releases all ending adapters");
        for(const auto mode:{"free","credits_2","credits_black",""})
        {
            state.mode=mode;state.credits=false;
            check(e::classify(state)==e::stage::none && !seq::ending::presentation(e::classify(state)).hide_chest_equipment,
                "museum and direct credits cannot inherit campaign camera, interaction or chest hiding");
        }
        state={"credits_1","crawl_extra",true};check(e::classify(state)==e::stage::none,"unknown/prefix entry cannot enter physical ending controls");
        for(const auto stage:{e::stage::grounded,e::stage::crawl,e::stage::pull,e::stage::throw_ready,e::stage::rescue})
        {
            const auto view=seq::ending::presentation(stage);
            check(view.camera==vr::game_view::camera_profiles::authored && view.rotation_tag==vr::game_view::scripted_camera_tag::player && view.block_carry && view.hide_chest_equipment,
                "authored finale phases reuse relative native yaw and block player storage/drop");
        }
        {
            using namespace vr::game_view;
            const auto axis=[](float degrees){const float r=degrees*vr::pose_filter::radians,c=std::cos(r),s=std::sin(r);
                return camera_axis{{{c,s,0},{-s,c,0},{0,0,1}}};};
            const auto yaw=[](const camera_output& out){return std::atan2(out.axis[0][1],out.axis[0][0])/vr::pose_filter::radians;};
            auto approach=seq::ending::presentation(e::stage::approach);approach.epoch=10;approach.position_epoch=20;
            auto request=seq::camera_request_for(approach);
            check(!request.epoch && !request.policy.owns_rotation() && approach.allow_movement && approach.allow_turn && approach.block_carry && approach.hide_chest_equipment,
                "approach shares normal HMD/locomotion command ownership while retaining the carry boundary");
            camera_rig camera;camera_input input{axis(70),axis(30),{.2f,.1f,.1f},70,30,30,100,1};
            check(std::abs(yaw(camera.compose(input,request))-70)<.001f,"approach renders the same heading used by native movement");
            input.time+=10;input.head_axis=axis(60);input.head_heading=input.command_head_heading=60;input.native_heading=100;input.native_axis=axis(100);
            check(std::abs(yaw(camera.compose(input,request))-100)<.001f,"physical head yaw remains in the gameplay view and movement basis");
            input.time+=10;input.native_heading=145;input.native_axis=axis(145);
            check(std::abs(yaw(camera.compose(input,request))-145)<.001f,"stick yaw turns the visible camera as well as locomotion");
            auto fight=seq::ending::presentation(e::stage::subdual);fight.epoch=10;fight.position_epoch=21;
            request=seq::camera_request_for(fight);scripted_rotation_reference tag{21,axis(90)};
            input.head_meters={.5f,.2f,.3f};++input.time;
            auto out=camera.compose(input,request,&tag);
            check(std::abs(yaw(out)-90)<.001f && out.head_offset==vec{},"subdual snaps once to original tag yaw/position instead of carrying approach offsets");
            input.time+=10;input.head_axis=axis(80);input.head_heading=80;tag.axis=axis(100);
            check(std::abs(yaw(camera.compose(input,request,&tag))-120)<.001f,"after subdual alignment head motion and native yaw accumulate without repeated snapping");
            for(const auto stage:{e::stage::wakeup,e::stage::wounded})
            {
                auto cut=seq::ending::presentation(stage);cut.epoch=10;cut.position_epoch=++tag.source;
                request=seq::camera_request_for(cut);tag.axis=axis(-40);++input.time;
                auto aligned=camera.compose(input,request,&tag);
                check(std::abs(yaw(aligned)+40)<.001f && aligned.head_offset==vec{},"wakeup and post-stomp body entries reset once to the original camera anchor");
                input.head_axis=axis(input.head_heading+10);input.head_heading+=10;++input.time;
                check(std::abs(yaw(camera.compose(input,request,&tag))+30)<.001f,"free head motion after the cut cannot retrigger alignment");
            }
        }
        {
            const float units=39.370098f;
            const vec gun{27855.2559f,34039.8867f,-9962.9102f};
            std::array<e::hand_sample,2> hands{};
            hands[0]={{add(gun,scale(vec{0,.2f,.3f},units)),{0,0,0,1}},true};
            check(e::crawl_reach(hands,gun,units).hands==1,"left wrist alone inside the revolver sphere completes crawl without a planted grip");
            hands[1]=hands[0];hands[0].valid=false;
            check(e::crawl_reach(hands,gun,units).hands==2,"right wrist still completes crawl when left tracking is unavailable");
            hands[1].wrist.position=add(gun,scale(vec{0,.3f,.35f},units));hands[0].wrist.position=gun;
            check(!e::crawl_reach(hands,gun,units).hands && e::crawl_reach(hands,gun,units).nearest_meters>.4f,
                "invalid wrist at the gun and valid wrist outside the 3D sphere cannot complete crawl");
            hands[0].valid=true;hands[0].wrist.position=add(gun,scale(vec{0,0,.5f},units));
            check(!e::crawl_reach(hands,gun,units).hands,"vertical distance is included for both tracked wrists");
            hands[0].wrist.position={.4f,0,0};hands[1].valid=false;
            check(e::crawl_reach(hands,{},1).hands==1,"the exact 40 cm boundary is included");
            hands[0].valid=false;
            check(!e::crawl_reach(hands,{},1).hands && e::crawl_reach(hands,{},1).nearest_meters<0,
                "missing tracking never inherits a previous arrival distance");
            hands[0].valid=true;
            check(!e::crawl_reach(hands,{},0).hands,"invalid world scale cannot satisfy arrival");
            const float early=e::crawl_height_blend(-.25f,.35f,50),late=e::crawl_height_blend(-.25f,.35f,600);
            check(e::crawl_height_blend(-.25f,.35f,0)==-.25f && early<-.23f && late>.33f &&
                std::abs(e::crawl_height_blend(-.25f,.35f,650)-.35f)<.0001f,
                "crawl inherits the previous eye height and settles smoothly before input instead of rising on its first frame");
        }
        for(const auto start:{"gun_fight","crawl","gun_kick","wounded","pullout","kill","endgame"})
            check(e::classify({"credits_1",start,true})!=e::stage::none,"direct native checkpoints enter the appropriate ending policy without earlier flags");
        for(const auto& grip:e::grips::second)check(length(grip.position)<8 && vr::gameplay::free_climb::rotation_valid(grip.rotation),"native pull grip fits the knife handle and has a valid anatomical basis");
        {
            e::pull_gesture pull;vr::controller_input::frame input;input.sequence=1;input.reference_generation=1;input.focused=true;input.sampled_at=vr::controller_input::clock::now();
            input.squeeze[1]={true,true,1,1};std::array<e::hand_sample,2> hands{};hands[1].valid=true;
            std::array<anchor,2> grips{};
            const auto tick=[&]{const auto work=pull.update(input,hands,grips,40,true);++input.sequence;input.sampled_at+=20ms;return work;};
            check(tick()==0 && !pull.held(),"pre-held grip cannot acquire a knife on phase entry");
            input.squeeze[1].down=false;tick();input.squeeze[1].down=true;++input.squeeze[1].presses;
            check(tick()==0 && pull.held()==2,"fresh nearby grip acquires without inventing extraction work");
            hands[1].wrist.rotation={std::sin(.05f),0,0,std::cos(.05f)};
            check(tick()>0,"deliberate wrist rotation advances held extraction");
            hands[1].wrist.position={100,0,0};check(tick()==0 && pull.held()==2,"held wrist stays leased at the knife even when the physical controller moves away");
            input.squeeze[1].down=false;hands[1].wrist.rotation={.1f,0,0,.994987f};check(tick()==0 && !pull.held(),"released rotation cannot accumulate progress");
            hands[1].wrist.position={};input.squeeze[1].down=true;++input.squeeze[1].presses;tick();
            input.focused=false;check(tick()==0 && !pull.held(),"focus loss releases the interaction and progress baseline");
            input.focused=true;check(tick()==0 && !pull.held(),"tracking recovery with held grip requires neutral rearm");
        }
        {
            e::throw_gesture gesture;
            check(!gesture.update({-.2f,0,0},{},{1,0,0},1,1,0,true),"hand behind the head cannot arm a throw on entry");
            check(!gesture.update({-.29f,0,0},{},{1,0,0},.6f,1,80,true),"short movement and wide gaze no longer trigger the native throw");
            gesture.reset();
            check(!gesture.update({.2f,0,0},{},{1,0,0},1,1,100,true),"fresh forward arm establishes preparation baseline");
            check(!gesture.update({.2f,0,0},{},{0,1,0},1,1,250,true),"target/native camera yaw alone supplies no physical draw-back");
            check(!gesture.update({.2f,0,0},{.2f,0,0},{1,0,0},1,1,260,true),"head-only movement cannot impersonate drawing back an arm");
            check(!gesture.update({0,0,0},{-.2f,0,0},{1,0,0},1,1,280,true),"whole-body motion cannot impersonate drawing back an arm");
            check(!gesture.update({.11f,0,0},{},{1,0,0},1,1,290,true),"short incidental rearward motion no longer throws");
            check(gesture.update({.03f,0,0},{},{1,0,0},1,1,300,true),"restored deliberate rearward arm travel accepts the native throw");
            check(!gesture.update({-.1f,0,0},{},{1,0,0},1,1,350,true),"held preparation commits at most one throw");
            gesture.reset();gesture.update({.2f,0,0},{},{1,0,0},1,1,0,true);
            check(!gesture.update({0,0,0},{},{1,0,0},.4f,1,300,true),"looking away cancels pending preparation");
            check(!gesture.update({0,0,0},{},{1,0,0},1,2,400,true),"recenter cannot inherit rearward travel");
        }
        {
            check(e::museum_camera("credits_2",false,true) && e::museum_camera("credits_1",true,true),"direct and campaign museum credits admit their exact native camera");
            check(!e::museum_camera("free",true,true) && !e::museum_camera("credits_black",true,true) &&
                !e::museum_camera("credits_1",false,true) && !e::museum_camera("credits_2",true,false),"museum roaming, black credits and unrelated links retain their original policy");
            const auto& camera=seq::scene_cameras::museum_credits;
            check(camera.head==vr::game_view::head_rotation::free && camera.translation==vr::game_view::head_translation::tracked,
                "museum credit viewing keeps free head rotation and unattenuated movement");
        }
    }
}
