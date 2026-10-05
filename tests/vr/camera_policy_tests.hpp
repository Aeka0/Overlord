#pragma once
#include "component/vr/camera_rig.hpp"
#include "component/vr/gameplay/scripted_sequences.hpp"
#include "component/vr/gameplay/sequences/estate.hpp"
#include "component/vr/gameplay/sequences/gulag.hpp"
#include "component/vr/gameplay/sequences/airport.hpp"
#include <limits>

template<class Check> void camera_policy_tests(Check check)
{
    using namespace vr::game_view;
    const auto axes=[](float pitch,float yaw,float roll) {
        const float p=pitch*vr::pose_filter::radians,y=yaw*vr::pose_filter::radians,r=roll*vr::pose_filter::radians;
        const float cp=std::cos(p),sp=std::sin(p),cy=std::cos(y),sy=std::sin(y),cr=std::cos(r),sr=std::sin(r);
        return camera_axis{{{cp*cy,cp*sy,-sp},{-sy*cr+sp*cy*sr,cy*cr+sp*sy*sr,cp*sr},
            {sy*sr+sp*cy*cr,-cy*sr+sp*sy*cr,cp*cr}}};
    };
    const auto heading=[](const camera_axis& a){return std::atan2(a[0][1],a[0][0])*57.29577951308232f;};
    const auto close=[](float a,float b){return std::abs(std::remainder(a-b,360.f))<.001f;};
    const auto same=[&](const camera_axis& a,const camera_axis& b){
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)if(!close(a[i][j],b[i][j]))return false;return true;
    };
    camera_input input{axes(0,170,0),vr::pose_filter::identity,{.4f,1.6f,0},170,0,0,100,1};
    camera_rig rig;
    {
        camera_rig remote;
        const auto request=vr::gameplay::sequences::camera_request_for({},0,0,17);
        camera_input view{axes(63,-80,3),vr::pose_filter::identity,{1,2,3},-80,0,0,100,1};
        auto frame=remote.compose(view,request);
        check(same(frame.axis,view.native_axis) && frame.head_offset==std::array<float,3>{},
            "UAV camera preserves complete native down-looking pitch and stays at its remote origin");
        view.native_axis=axes(75,-70,5);view.head_axis=axes(-4,2,0);++view.time;
        frame=remote.compose(view,request);
        check(same(frame.axis,vr::pose_filter::multiply(view.head_axis,view.native_axis)),
            "native missile pitch and only the unconsumed head correction compose once");
    }
    {
        using namespace vr::gameplay::sequences;
        airport::evidence native{true,false,41,41,"player_ending"};
        auto scene=airport::classify(native);scene.epoch=80;scene.position_epoch=801;
        camera_rig ending_camera;
        camera_input tracked{axes(0,170,0),axes(15,40,12),{.4f,1.7f,-.2f},170,40,40,100,1};
        scripted_rotation_reference tag{801,axes(-10,-60,8)};
        const auto initial_head=tracked.head_axis;
        (void)ending_camera.compose(tracked,camera_request_for(scene));
        auto frame=ending_camera.compose(tracked,camera_request_for(scene),&tag);
        check(same(frame.axis,tag.axis) && frame.head_offset==std::array<float,3>{},
            "airport boarding waits for its tag then aligns position and all axes despite an off-center physical head");
        tracked.head_axis=axes(-20,75,25);tracked.head_heading=75;tracked.head_meters[0]+=.2f;++tracked.time;
        tag.axis=axes(-15,-50,13);
        frame=ending_camera.compose(tracked,camera_request_for(scene),&tag);
        const auto expected=vr::pose_filter::multiply(vr::pose_filter::multiply(tracked.head_axis,
            vr::pose_filter::transpose(initial_head)),tag.axis);
        check(same(frame.axis,expected) && std::abs(frame.head_offset[0]-.02f)<.0001f,
            "before the shot airport retains relative free observation and attenuated movement without repeated alignment");
        native.shot=true;scene=airport::classify(native);scene.epoch=80;scene.position_epoch=801;
        tag.axis=axes(55,-35,75);++tracked.time;
        frame=ending_camera.compose(tracked,camera_request_for(scene),&tag);
        check(same(frame.axis,tag.axis) && frame.head_offset==std::array<float,3>{} && !frame.spatial,
            "Makarov's shot on the same rig immediately discards every physical angle and position offset");
        tracked.head_axis=axes(80,160,-60);tracked.head_heading=160;tracked.head_meters={2,3,4};
        ++tracked.reference;++tracked.time;tag.axis=axes(85,20,110);
        frame=ending_camera.compose(tracked,camera_request_for(scene),&tag);
        check(same(frame.axis,tag.axis) && frame.head_offset==std::array<float,3>{},
            "shot camera follows native yaw pitch and roll through head motion and recenter without freezing the animation");
        ++tracked.time;
        frame=ending_camera.compose(tracked,camera_request_for(scene));
        check(same(frame.axis,tag.axis) && frame.head_offset==std::array<float,3>{},
            "temporary loss of the shot tag cannot release head tracking or replace the last authored basis");
        ++tracked.time;tag.axis=axes(100,30,120);
        check(same(ending_camera.compose(tracked,camera_request_for(scene),&tag).axis,tag.axis),
            "restored airport tag resumes exact full rotation including pitch beyond vertical");
        tracked.time=50;native.shot=false;scene=airport::classify(native);scene.epoch=81;scene.position_epoch=802;
        tag={802,axes(-5,-65,4)};
        frame=ending_camera.compose(tracked,camera_request_for(scene),&tag);
        check(same(frame.axis,tag.axis) && frame.spatial && frame.head_offset==std::array<float,3>{},
            "checkpoint reload rearms boarding alignment and removes the previous shot lock");
    }
    scripted_rotation_reference source{11,axes(10,25,15)};
    auto dragging=vr::gameplay::sequences::estate::classify(true,true,"worldbody",true);
    dragging.epoch=1;dragging.position_epoch=21;
    check(close(heading(rig.compose(input,vr::gameplay::sequences::camera_request_for(dragging)).axis),170),
        "Estate dragging combat keeps player-owned view before the authored cut");
    auto ending=vr::gameplay::sequences::estate::classify(true,true,"worldbody",false);
    ending.epoch=1;ending.position_epoch=21;
    camera_request request=vr::gameplay::sequences::camera_request_for(ending);
    check(close(heading(rig.compose(input,request).axis),170) && request.policy.needs_tag(),
        "Estate handoff waits for the raw tag without consuming its one-time alignment");
    auto out=rig.compose(input,request,&source);
    check(close(heading(out.axis),25),"Shepherd entry uses original tag heading instead of the rear-facing native clamp");
    input.native_heading=-130;input.native_axis=axes(40,-130,60);input.time+=10;
    source.axis=axes(40,35,60);out=rig.compose(input,request,&source);
    check(close(heading(out.axis),35),"Shepherd inherits native yaw while native pitch, roll and clamps stay independent");
    input.time+=10;source={12,axes(0,-90,0)};ending.position_epoch=22;
    request=vr::gameplay::sequences::camera_request_for(ending);
    check(close(heading(rig.compose(input,request,&source).axis),-90),"a new authored shot resets once even inside one story epoch");
    source.axis=axes(0,-80,0);input.time+=10;
    check(close(heading(rig.compose(input,request,&source).axis),-80),"entry alignment does not keep reseeding later animation frames");
    auto combat=request;combat.policy=vr::gameplay::sequences::scene_cameras::estate_drag;combat.epoch=0;
    (void)rig.compose(input,combat);source.axis=axes(0,60,0);input.time+=10;
    check(close(heading(rig.compose(input,request,&source).axis),60),
        "leaving aligned policy rearms a new cinematic phase even on the same native body");

    {
        // Live Gulag witness: the obsolete tag_turret helper has 119.20 deg
        // roll, while the linked remastered tag_player has only 8.56 deg.
        // They share a sequence epoch. Banking from the helper must never seed
        // that epoch, nor consume the real camera's one-time alignment.
        namespace gulag=vr::gameplay::sequences::gulag;
        auto legacy=gulag::classify({.alive = true, .linked = true, .intro_controller = true, .tag = scripted_camera_tag::aim});
        legacy.epoch=55;legacy.position_epoch=550;
        camera_rig switching;
        camera_input view{axes(0,170,0),axes(12,30,5),{},170,30,30,100,1};
        scripted_rotation_reference helper{550,axes(71.14892f,-160.0584f,119.1992f)};
        auto legacy_request=vr::gameplay::sequences::camera_request_for(legacy);
        check(legacy.camera.axes==script_axes::yaw && legacy.camera.entry==camera_entry::preserve &&
            same(switching.compose(view,legacy_request,&helper).axis,axes(12,170,5)),
            "legacy Gulag aim helper cannot inject its turret roll or consume the camera cut");
        ++view.time;helper.axis=axes(71.14892f,-140.0584f,119.1992f);
        check(same(switching.compose(view,legacy_request,&helper).axis,axes(12,-170,5)),
            "legacy helper retains authored yaw without accepting its unrelated pitch/roll");
        auto animated=gulag::classify({.alive = true, .linked = true, .intro_controller = true, .tag = scripted_camera_tag::player});
        animated.epoch=55;animated.position_epoch=551;
        const auto animated_request=vr::gameplay::sequences::camera_request_for(animated);
        helper={551,axes(2.11236f,-130.58766f,8.56289f)};++view.time;
        const auto relative_head=vr::pose_filter::multiply(view.head_axis,axes(0,-30,0));
        check(animated_request.entry_epoch==legacy_request.entry_epoch &&
            same(switching.compose(view,animated_request,&helper).axis,
                vr::pose_filter::multiply(relative_head,axes(0,-130.58766f,8.56289f))),
            "remastered Gulag entry aligns its real bank without retaining the legacy sideways basis");
        helper.axis=axes(40,-120.58766f,12.56289f);++view.time;
        check(same(switching.compose(view,animated_request,&helper).axis,
            vr::pose_filter::multiply(relative_head,axes(0,-120.58766f,12.56289f))),
            "corrected Gulag camera still follows native yaw/roll while excluding native pitch");
    }

    auto helicopter=vr::gameplay::sequences::gulag::classify({.alive = true, .linked = true, .intro_controller = true});
    helicopter.epoch=60;helicopter.position_epoch=601;
    request=vr::gameplay::sequences::camera_request_for(helicopter);
    rig={};input={axes(0,170,0),axes(12,30,5),{},170,30,30,100,1};
    check(same(rig.compose(input,request).axis,axes(12,170,5)),
        "Gulag waits for its authored tag instead of consuming entry with a clamped native heading");
    source={601,axes(70,25,60)};
    const auto entry_head=input.head_axis;
    const auto entry_heading=axes(0,-input.head_heading,0);
    check(same(rig.compose(input,request,&source).axis,vr::pose_filter::multiply(vr::pose_filter::multiply(entry_head,entry_heading),axes(0,25,60))),
        "Gulag opening aligns native yaw/roll while preserving physical tilt instead of adopting native pitch");
    input.time=110;input.head_axis=axes(12,45,5);input.head_heading=45;
    source.axis=axes(70,35,60);
    const auto followed=vr::pose_filter::multiply(vr::pose_filter::multiply(input.head_axis,entry_heading),axes(0,35,60));
    check(same(rig.compose(input,request,&source).axis,followed),
        "Gulag follows aircraft yaw/roll while preserving free physical head motion");
    input.time=120;helicopter.position_epoch=602;
    request=vr::gameplay::sequences::camera_request_for(helicopter);source={602,axes(0,-120,0)};
    check(request.entry_epoch==60 && request.position_epoch==602 &&
        same(rig.compose(input,request,&source).axis,followed),
        "Gulag source replacement after remastered entry cannot reset the player's chosen heading");
    input.time=130;source.axis=axes(0,-110,0);
    check(same(rig.compose(input,request,&source).axis,vr::pose_filter::multiply(followed,axes(0,10,0))),
        "Gulag parent replacement resumes the new source's relative transform without another cut");
    input.time=50;source.axis=axes(0,90,0);
    check(same(rig.compose(input,request,&source).axis,axes(12,90,5)),
        "Gulag checkpoint rollback rearms the one-time entry alignment");

    auto evacuation=vr::gameplay::sequences::gulag::evacuation({.alive = true, .linked = true, .rig = "player_rig", .begun = true, .used = true});
    evacuation.epoch=70;evacuation.position_epoch=701;
    request=vr::gameplay::sequences::camera_request_for(evacuation);rig={};
    input={axes(0,180,0),axes(15,45,7),{.4f,1.7f,-.2f},180,45,45,200,3};source={701,axes(25,-30,20)};
    out=rig.compose(input,request,&source);
    const auto rope_heading=axes(0,-input.head_heading,0);
    check(same(out.axis,vr::pose_filter::multiply(vr::pose_filter::multiply(input.head_axis,rope_heading),axes(0,-30,20))) && out.head_offset==std::array<float,3>{},
        "rope entry keeps physical tilt and exact original position, without imposing scripted pitch");
    input.head_axis=axes(30,60,12);input.head_heading=60;input.head_meters={2,2,2};input.time+=10;
    source.axis=axes(40,-10,45);
    const auto rope_follow=vr::pose_filter::multiply(vr::pose_filter::multiply(input.head_axis,rope_heading),axes(0,-10,45));
    out=rig.compose(input,request,&source);
    check(same(out.axis,rope_follow) && out.head_offset==std::array<float,3>{},
        "rope camera follows native yaw/roll and free observation without physical translation separating the carabiner");
    source.axis=axes(-70,-10,45);++input.time;
    check(same(rig.compose(input,request,&source).axis,rope_follow),"native pitch-only motion cannot tilt the rope camera after entry");
    evacuation.position_epoch=702;request=vr::gameplay::sequences::camera_request_for(evacuation);
    source={702,axes(-5,130,-30)};input.time+=10;
    check(request.entry_epoch==70 && same(rig.compose(input,request,&source).axis,rope_follow),
        "temporary rope rig to main rig handoff belongs to one alignment lifetime");

    for(int direction:{-1,1})
    {
        camera_rig filtered;camera_request reduced{camera_profiles::authored_yaw_roll,75,750,75};
        camera_input neutral{axes(0,0,0),vr::pose_filter::identity,{},0,0,0,300,1};
        scripted_rotation_reference camera{750,axes(0,20,35)};
        const auto basis=filtered.compose(neutral,reduced,&camera).axis;
        for(int pitch=5;pitch<=120;pitch+=5)
        {
            ++neutral.time;camera.axis=axes(float(direction*pitch),20,35);
            check(same(filtered.compose(neutral,reduced,&camera).axis,basis),
                "discarded native pitch crossing a vertical pole cannot manufacture yaw or roll");
        }
        neutral.head_axis=axes(30,0,0);++neutral.time;
        check(same(filtered.compose(neutral,reduced,&camera).axis,vr::pose_filter::multiply(neutral.head_axis,basis)),
            "excluding native pitch leaves physical head pitch fully responsive");
    }

    load_recenter loaded;
    check(!loaded.consume(1,false,true) && loaded.consume(1,true,true) && !loaded.consume(1,true,true),
        "scripted load waits for valid observation then resets exactly once");
    check(!loaded.consume(2,true,false) && !loaded.consume(2,true,true),
        "ordinary load cannot recenter a much later cinematic");
    check(loaded.consume(3,true,true) && !loaded.consume(3,true,true),
        "new checkpoint lifecycle rearms the scripted load reset");

    rig={};request={camera_profiles::bounded,2,30};request.policy.limits={{-10,-15,-5},{10,15,5}};
    input={axes(0,40,0),vr::pose_filter::identity,{},40,0,0,200,1};
    (void)rig.compose(input,request);
    input.head_axis=axes(30,60,20);input.head_heading=60;input.time+=10;
    out=rig.compose(input,request);
    check(same(out.axis,vr::pose_filter::multiply(axes(10,15,5),axes(0,40,0))),"bounded head motion clamps all three axes in its entry frame");
    // The tracking reference is horizontal; recenter removes yaw, retaining
    // the same physical pitch/roll exactly as the production bridge does.
    input.reference=2;input.head_axis=axes(30,0,20);input.head_heading=0;input.time+=10;
    check(same(rig.compose(input,request).axis,out.axis),"recenter preserves the bounded view instead of releasing its limit");
    request.policy.limits.minimum[1]=500;request.policy.limits.maximum[1]=600;input.time+=10;
    check(vr::pose_filter::valid({{},rig.compose(input,request).axis}),"extreme or malformed angle limits cannot create an invalid rotation");

    rig={};request={camera_profiles::bounded,20,31};request.policy.limits={{-10,-15,-5},{10,15,5}};
    input={axes(0,40,0),vr::pose_filter::identity,{},40,0,0,250,1};(void)rig.compose(input,request);
    for(int y=5;y<=220;y+=5){input.head_axis=axes(0,float(y),0);input.head_heading=float(y);++input.time;out=rig.compose(input,request);}
    check(close(heading(out.axis),55),"turning beyond 180 degrees cannot flip a bounded view to the opposite limit");
    rig={};input.head_axis=vr::pose_filter::identity;input.head_heading=0;(void)rig.compose(input,request);
    for(int p=5;p<=110;p+=5){input.head_axis=axes(float(p),0,0);++input.time;out=rig.compose(input,request);}
    check(same(out.axis,vr::pose_filter::multiply(axes(10,0,0),axes(0,40,0))),"looking through the vertical pole cannot manufacture bounded yaw or roll");

    rig={};request={camera_profiles::free,3};request.policy.head=head_rotation::pitch_roll;
    input={axes(0,40,0),axes(20,60,10),{},40,60,0,300,1};
    out=rig.compose(input,request);
    check(same(out.axis,vr::pose_filter::multiply(axes(20,0,10),axes(0,40,0))),"pitch-roll head policy excludes physical yaw independently of script influence");

    rig={};request={camera_profiles::free,4,40};request.policy.script=script_rotation::additive;request.policy.axes=script_axes::all;
    request.policy.source=rotation_source::tag;
    input={axes(0,40,0),vr::pose_filter::identity,{},40,0,0,400,1};source={41,axes(0,0,0)};
    (void)rig.compose(input,request,&source);source.axis=axes(20,10,15);input.time+=10;
    out=rig.compose(input,request,&source);
    check(same(out.axis,vr::pose_filter::multiply(axes(0,40,0),source.axis)),"full additive script motion composes transforms rather than summing Euler angles");
    check(same(rig.compose(input,request,&source).axis,out.axis),"repeated full-transform samples cannot apply a turn twice");
    input.reference=2;input.time+=10;input.head_axis=axes(10,30,5);input.head_heading=30;
    check(same(rig.compose(input,request,&source).axis,out.axis),"full additive transform survives recenter without a camera jump");

    scripted_position position;auto policy=camera_profiles::free;
    check(position.offset(50,1,{1,2,3},policy)==std::array<float,3>{},"attenuated motion anchors the physical head on entry");
    check(close(position.offset(50,1,{2,2,3},policy)[0],.05f),"attenuated movement respects its metre limit");
    policy.translation=head_translation::tracked;
    check(close(position.offset(50,1,{2,2,3},policy)[0],1),"real displacement bypasses attenuation and its cap");
    policy.translation=head_translation::fixed;
    check(position.offset(50,1,{2,2,3},policy)==std::array<float,3>{},"fixed displacement suppresses head translation independently of head rotation");
    check(position.offset(0,1,{std::numeric_limits<float>::quiet_NaN(),0,0},policy)==std::array<float,3>{},"invalid translation never enters camera arithmetic");

    // Camera cuts use raw source headings, even when player angles still
    // contain the previous shot's clamp and HMD contribution.
    using namespace vr::gameplay::sequences;
    rig={};request={scene_cameras::dcemp_space,30,300,300};
    input={axes(0,170,0),axes(12,30,5),{},170,30,0,500,1};
    (void)rig.compose(input,request); // The client tag may arrive after the server link.
    source={301,axes(70,-70,50)};
    out=rig.compose(input,request,&source);
    check(same(out.axis,axes(12,-70,5)),"ISS cuts to the raw camera heading once while preserving physical pitch and roll");
    input.time=510;input.head_axis=axes(12,45,5);input.head_heading=45;source.axis=axes(10,100,20);
    check(same(rig.compose(input,request,&source).axis,axes(12,-55,5)),
        "ISS entry alignment does not follow later scripted rotation or cancel free head turning");

    request={scene_cameras::dcemp_ground,0,302,302};source={303,axes(0,70,0)};
    input.time=520;input.native_heading=-130;
    check(same(rig.compose(input,request,&source).axis,axes(12,70,5)),
        "ground return replaces the ISS heading without acquiring scripted input ownership");
    float command_yaw=-130;
    check(rig.restore_command(0,1,command_yaw,10,45,45,input.head_axis) && close(command_yaw,60),
        "one-shot ground alignment rebases native yaw including the engine delta");
    rig.record(530);
    check(same(rig.compose(input,request,&source).axis,axes(12,70,5)),
        "older prediction holds the corrected return view until its rebased command is consumed");
    input.time=530;input.native_heading=90;input.command_head_heading=45;source.axis=axes(0,-120,0);
    check(same(rig.compose(input,request,&source).axis,axes(12,90,5)) &&
        !rig.restore_command(0,1,command_yaw,10,45,45,input.head_axis),
        "after return native turning resumes and repeated tag samples never reset the view again");
    input.time=100;source.axis=axes(0,40,0);
    check(same(rig.compose(input,request,&source).axis,axes(12,40,5)),
        "checkpoint time rollback allows a fresh player-owned entry alignment");

    vr::gameplay::sequences::view scene;scene.camera=camera_profiles::authored;scene.epoch=8;scene.position_epoch=9;
    const auto selected=vr::gameplay::sequences::camera_request_for(scene,10,11);
    check(selected.policy==camera_profiles::fixed_scope && selected.epoch==(10|(1ull<<63)) && !selected.position_epoch,
        "native scope priority is identical for command suppression and final rendering");
    scene={};scene.camera=vr::gameplay::sequences::scene_cameras::physical_ladder;scene.position_epoch=100;
    rig={};input.head_meters={.2f,-.3f,.15f};
    const auto before=rig.compose(input,{});
    const auto grasped=rig.compose(input,vr::gameplay::sequences::camera_request_for(scene));
    check(grasped.head_offset==before.head_offset,"ladder entry preserves the existing horizontal and vertical head offset");
    ++scene.position_epoch;input.head_meters[0]+=.1f;
    const auto moved=rig.compose(input,vr::gameplay::sequences::camera_request_for(scene));
    check(close(moved.head_offset[0],.3f) && close(moved.head_offset[1],-.3f),
        "linked-object bookkeeping cannot re-anchor or clamp player-owned tracking");

    {
        // Endgame witness: the same helper was classified as 139 new shots
        // in 7.9 seconds because its temporary VM wrapper kept changing.
        view previous;previous.camera=scene_cameras::ending_wounded;
        previous.scene=scenario::ending;previous.epoch=5;previous.position_epoch=20;
        previous.linked_entity=875;previous.linked_generation=9;previous.linked_object=400;
        previous.player=0x1000;previous.command_time=100;
        camera_rig stable;camera_input tracked{axes(0,30,0),axes(0,0,0),{},30,0,0,100,1};
        scripted_rotation_reference tag{40,axes(0,30,0)};
        (void)stable.compose(tracked,camera_request_for(previous),&tag);
        for(unsigned i=1;i<=20;++i)
        {
            auto next=previous;next.linked_object+=17;next.command_time+=50;
            const bool same=same_position_owner(previous,next,next.player,next.command_time,true);
            check(same,"VM wrapper churn does not replace a living native camera entity");
            next.position_epoch=same?previous.position_epoch:previous.position_epoch+1;
            tracked.head_heading=float(i)*2;tracked.head_axis=axes(0,tracked.head_heading,0);
            tracked.head_meters[0]=float(i)*.01f;tracked.time=next.command_time;
            const auto frame=stable.compose(tracked,camera_request_for(next),&tag);
            check(close(heading(frame.axis),30+tracked.head_heading) && std::abs(frame.head_offset[0]-float(i)*.001f)<.0001f,
                "head turning and translation remain continuous across server observations of a temporary helper");
            previous=next;
        }
        auto replacement=previous;++replacement.linked_generation;
        check(!same_position_owner(previous,replacement,previous.player,previous.command_time,true),"reusing a native entity slot still establishes a new camera lifetime");
        replacement=previous;++replacement.linked_entity;
        check(!same_position_owner(previous,replacement,previous.player,previous.command_time,true),"a real helper-to-body handoff establishes a new camera lifetime");
        check(!same_position_owner(previous,previous,previous.player,previous.command_time-1,true) &&
            !same_position_owner(previous,previous,previous.player,previous.command_time,false),"checkpoint rewind and level changes still reset camera ownership");
    }
}
