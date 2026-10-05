#pragma once
#include "component/vr/gameplay/signal_flare_policy.hpp"
#include "component/vr/gameplay/signal_flare_profile.hpp"
#include "component/vr/gameplay/signal_flare_script.hpp"
#include "component/vr/hud_prompts.hpp"
#include "component/game_text.hpp"
#include "component/vr/gameplay/hand_interaction/pose_plan.hpp"
#include "component/vr/gameplay/followed_fx_policy.hpp"
#include "game/assets.hpp"

template<class Check>void signal_flare_tests(Check& check)
{
    using namespace vr::gameplay;namespace flare=equipment::special::flare;
    using vr::hand;using namespace hands;using namespace vr::gameplay::hands::pose_math;
    check((0x10000046u & game::FX_ELEM_RUN_MASK)==game::FX_ELEM_RUN_RELATIVE_TO_EFFECT &&
        (0x10000086u & game::FX_ELEM_RUN_MASK)==game::FX_ELEM_RUN_RELATIVE_TO_CAMERA &&
        game::FX_ELEM_RUN_RELATIVE_TO_SPAWN==0 && game::FX_ELEM_RUN_RELATIVE_TO_WORLD==0x100,
        "captured H2 flame flags select the effect frame; the previous 0x80 clone selected the camera frame");
    {
        // The same 200 ms contact grace uses the chosen physical button;
        // releasing grip must not cancel a trigger-acquired cap (and vice versa).
        vr::controller_input::frame input{};input.focused=true;input.reference_generation=1;
        input.sampled_at=vr::controller_input::clock::time_point{}+std::chrono::seconds(1);
        input.trigger[0].active=input.trigger[0].down=true;
        equipment::grab_intent trigger,grip;
        check(trigger.consume(input,1,1,0,input.trigger)==1 && grip.consume(input,1,1,0)==0,
            "trigger cap intent is independent of the released grip button");
        input.sampled_at+=std::chrono::milliseconds(190);
        check(trigger.consume(input,1,0,0,input.trigger)==1,"trigger can precede actual cap contact within the shared intent window");
        input.sampled_at+=std::chrono::milliseconds(11);
        check(trigger.consume(input,1,0,0,input.trigger)==0,"holding trigger far from the cap cannot keep an unlimited ignition intent");
        input.trigger[0].down=false;
        check(trigger.consume(input,1,0,1,input.trigger)==0,"trigger release cancels its own pending cap acquisition");
        input.squeeze[0].active=input.squeeze[0].down=true;
        check(grip.consume(input,1,1,0)==1,"existing grip intent remains available with trigger released");
    }
    {
        // Model the native dispatch boundary, not recursive hook chaining:
        // vm_execute_stub replaces r14 and immediately decodes *r14. The next
        // opcode enters the hook dispatcher only on the next interpreter turn.
        const auto run_waiter=[&](unsigned observer,bool vr) {
            struct trace {bool waiting{};unsigned native_allowfire{},prepared{},waits{};} result;
            unsigned pc=flare::mission::allowfire_begin;
            for(unsigned step=0;step<16;++step)
            {
                if(vr && pc==flare::mission::allowfire_begin)pc=flare::mission::waiter_prepare;
                else if(vr && pc==observer)result.waiting=true;
                switch(flare::mission::signature[pc])
                {
                case 0xa0:pc+=2;break; // GetByte
                case 0x55:pc+=3;break; // EvalLevelFieldVariable
                case 0xaa:++result.native_allowfire;pc+=3;break; // CallBuiltinMethod1
                case 0x6a:++pc;break; // DecTop
                case 0x89:++result.prepared;++pc;break; // PreScriptCall
                case 0x53:pc+=5;break; // GetString
                case 0x2e:++result.waits;return result; // Original waittill_any_return
                default:return result;
                }
            }
            return result;
        };
        const auto old=run_waiter(flare::mission::waiter_prepare,true);
        check(!old.waiting && old.prepared==1 && old.waits==1,
            "original redirect reaches the native waiter but skips the observer placed on its target opcode");
        const auto vr=run_waiter(flare::mission::waiter_observe,true);
        check(vr.waiting && vr.prepared==1 && vr.waits==1 && vr.native_allowfire==0,
            "VR authorization observes the actual waiter path after skipping the native allowfire block");
        const auto flat=run_waiter(flare::mission::waiter_observe,false);
        check(!flat.waiting && flat.native_allowfire==1 && flat.prepared==1 && flat.waits==1,
            "flat mode preserves its original call and wait without authorizing physical cap removal");
        for(auto h:{hand::left,hand::right})
        {
            flare::state s;s.take(h);const auto other=hand(1-int(h));
            check(!s.start_pull(other,{},false) && s.start_pull(other,{},vr.waiting),
                "either hand can acquire the cap only after the corrected story waiter boundary");
        }
    }
    for(auto holder:{hand::left,hand::right})
    {
        flare::state s;const auto other=hand(1-int(holder));
        check(s.take(holder) && !s.take(other),"flare is one abdominal item, available before story authorization");
        check(!s.start_pull(other,{},false) && !s.ignite(true),"early pickup, trigger or a fabricated completion cannot ignite without another hand on the cap");
        check(!s.start_pull(holder,{},true),"one hand cannot hold both the tube and its cap");
        check(s.start_pull(other,{1,2,3},true),"the other hand may grasp the authorized cap");
        check(!s.handoff(other),"cap grasp cannot also transfer the tube");
        check(!s.sample_pull({1,2,3},40,true) && !s.sample_pull({1,2,2},40,true),"stationary hands and pushing the cap cannot ignite");
        check(!s.sample_pull({1,2,4.59f},40,true) && s.sample_pull({1,2,4.61f},40,true),"pulling four centimetres along the real cap axis requests ignition");
        check(!s.ignite(false) && !s.lit,"rejected native transaction retains an unlit item");
        check(s.ignite(true) && !s.ignite(true) && s.lit && s.cap_removed,"one native acceptance removes the cap and cannot be committed again");
        check(flare::burning(s,1000,1000) && flare::burning(s,1000+flare::burn_msec-1,1000) &&
            !flare::burning(s,1000+flare::burn_msec,1000) && !flare::burning(s,20000,1000) && s.held() && s.holder==holder && s.cap_removed,
            "burnout and later story time end only emission, retaining the physical tube and removed cap");
        check(s.handoff(other) && s.lit,"either hand can retain the lit tube without reigniting it");
        s.release(true);check(s.stage==flare::phase::dropped && !s.take(holder),"deliberate release drops a lit flare without refilling the belt");
        s.reset();s.take(holder);s.start_pull(other,{},true);s.release(false);
        check(s.stage==flare::phase::stowed && !vr::valid_hand(s.opener) && s.take(other),"interrupted safe pull releases both leases and returns the original flare");
        s.start_pull(holder,{},true);check(!s.sample_pull({0,0,2},40,false) && !s.ignite(true),"authorization loss cancels a previously acquired cap");
        s.start_pull(holder,{},true);check(!s.sample_pull({5,0,2},40,true) && !vr::valid_hand(s.opener),"sideways separation breaks the cap grasp without igniting");
        s.start_pull(holder,{},true);check(!s.sample_pull({0,0,100},40,true),"tracking jump cannot masquerade as a valid pull");
        s.start_pull(holder,{},true);check(!s.sample_pull({NAN,0,2},40,true),"invalid tracking cancels without NaN state");
        s.reset(true);check(s.stage==flare::phase::spent && !s.take(holder),"checkpoint with native completed flag cannot duplicate a new signal flare");
    }
    const auto close=[](vec a,vec b){return length(sub(a,b))<.0001f;};
    {
        native_followed_fx::state effect;anchor pose{{1,2,3},{0,0,0,1}};
        check(effect.begin(11,1000,pose) && !effect.begin(11,1010,pose),"one ignition can queue only one native looping effect");
        for(unsigned frame=1;frame<=90;++frame)
        {
            pose.position[0]=float(frame);pose.rotation=normalize(quat{0,std::sin(float(frame)*.01f),0,std::cos(float(frame)*.01f)});
            effect.position(11,pose);
            check(close(effect.pose.position,pose.position) && close(rotate(effect.pose.rotation,{1,0,0}),rotate(pose.rotation,{1,0,0})),
                "every render-pose publication updates the existing emitter without a 100 ms emission clock");
        }
        const auto valid=effect.pose;
        effect.position(10,{{100,200,300},{0,0,0,1}});effect.position(11,{{NAN,0,0},{0,0,0,1}});
        check(close(effect.pose.position,valid.position) && effect.follows(1000) && !effect.follows(900),
            "stale activation, invalid tracking and a recycled native effect birth cannot retarget an emitter");
        effect.active=false;check(!effect.follows(1000) && !effect.begin(11,1000,pose),"cancelled emission cannot be restarted by an old activation");
        check(effect.begin(12,2000,pose) && !effect.follows(1000) && effect.follows(2000),"new ignition retains a separate native effect lifetime");
    }
    check(flare::authored::left_cap_attachment.position[0]>2.9f && flare::authored::left_cap_attachment.position[0]<3.1f &&
        flare::authored::cap_fingers.size()==15 && flare::authored::cap_fingers[0].name.find("_le_")!=std::string_view::npos,
        "cap uses the native pre-separation left-hand grasp instead of a shifted tube grip");
    // Native model witness: both rigid meshes retain model-space vertices;
    // j_flare is identity, j_striker_cap is +Z 4.970859 (not a mesh origin).
    const anchor cap_bind{{0,0,4.970859f},{0,0,0,1}};
    const vec flare_center{0,0,-.030731916f};constexpr float flare_half_height=1.017061949f;
    for(float units:{25.f,40.f,80.f})for(float yaw:{0.f,.7f,2.f})
    {
        vr::head_pose_bridge::spatial_frame body;
        body.head_position={31,12,90};body.units_per_meter=units;
        body.head_yaw_axis={{{std::cos(yaw),std::sin(yaw),0},{-std::sin(yaw),std::cos(yaw),0},{0,0,1}}};
        const auto slot=flare::stowed_slot(body,flare_half_height);
        const auto tube=flare::stowed(body,flare_center,flare_half_height);
        check(close(compose(tube,{flare_center,{0,0,0,1}}).position,slot.position),"assembled flare bounds stay centered on the lowered pickup slot");
        const auto knife=equipment::knife_profile::stowed(equipment::locate_chest(body));
        const auto tip=compose(knife,{equipment::knife_profile::blade_tip,{0,0,0,1}}).position;
        check(tip[2]-(slot.position[2]+flare_half_height)>=units*.03f-.0001f,
            "horizontal flare clears the complete knife blade across body yaw and world scales");
        const auto mesh=rigid_delta(compose(tube,cap_bind),cap_bind);
        for(vec point:std::array<vec,2>{cap_bind.position,vec{.5f,.25f,6.4f}})
            check(close(compose(mesh,{point,{0,0,0,1}}).position,compose(tube,{point,{0,0,0,1}}).position),
                "stowed cap source vertices assemble with the tube without applying the cap bind twice");
        for(unsigned h=0;h<2;++h)
            check(equipment::special::abdominal_grab_distance_at(body,slot.position,{slot.position,{0,0,0,1}},{0,0,0,1},{0,0,0,1},h)<=1,
                "both hands can take the flare at its actual lowered storage position");
        if(units==40 && yaw==0)
        {
            check(slot.position[2]<equipment::special::abdomen(body).position[2]-units*.12f,
                "native flare and knife dimensions require about thirteen centimetres of extra clearance");
            const auto below=add(slot.position,{0,0,-units*.10f});
            check(equipment::special::abdominal_grab_distance_at(body,slot.position,{below,{0,0,0,1}},{0,0,0,1},{0,0,0,1},1)<=1 &&
                equipment::special::abdominal_grab_distance(body,{below,{0,0,0,1}},{0,0,0,1},{0,0,0,1},1)>1,
                "lowered flare reach moves with its model instead of relying on the old abdominal trigger");
        }
    }
    for(unsigned h=0;h<2;++h)
    {
        const auto a=flare::authored::attachment(h,{0,0,0,1});
        const anchor raw{{4,5,6},{0,.70710678f,0,.70710678f}};
        const auto tube=compose(raw,a),recovered=compose(tube,inverse(a));
        check(close(recovered.position,raw.position) && close(rotate(recovered.rotation,{1,0,0}),rotate(raw.rotation,{1,0,0})),"tube transform and inverse share the same anatomical wrist pivot on both hands");
        const auto rotated=compose(anchor{{},normalize(quat{.3f,.2f,.4f,.8f})},tube);
        const auto cap=compose(tube,{{0,0,4.970859f},{0,0,0,1}});
        for(float travel:{0.f,1.6f})
        {
            auto pulled=cap;pulled.position=add(pulled.position,rotate(tube.rotation,{0,0,travel}));
            const auto mesh=rigid_delta(pulled,cap_bind);
            check(close(compose(mesh,cap_bind).position,pulled.position),
                "held and pulling caps place their native pivot at the desired bone pose on either wrist");
        }
        const auto relative=compose(inverse(rotated),compose(anchor{{},normalize(quat{.3f,.2f,.4f,.8f})},cap));
        check(close(relative.position,{0,0,4.970859f}),"moving both hands together changes no cap stroke");
        const auto default_cap=flare::authored::cap_attachment(h,{0,0,0,1});
        const anchor cap_world{{1,2,3},{0,0,0,1}};
        for(auto turn:std::array<quat,4>{{{0,0,0,1},{0,0,.70710678f,.70710678f},{0,0,1,0},{0,0,-.70710678f,.70710678f}}})
        {
            const auto wanted=compose(cap_world,inverse(compose(default_cap,{{},turn})));
            const auto selected=flare::authored::choose_cap_attachment(h,{0,0,0,1},wanted.rotation,cap_world.rotation);
            const auto recovered_cap=compose(wanted,selected);
            check(close(recovered_cap.position,cap_world.position) && close(rotate(recovered_cap.rotation,{1,0,0}),{1,0,0}),
                "cap acquisition keeps the nearest wrist approach and the same rigid attachment after separation");
            const auto detached=rigid_delta(recovered_cap,cap_bind);
            check(close(compose(detached,cap_bind).position,cap_world.position),
                "detached cap geometry follows its latched grasp without the source bind offset");
        }
    }
    {
        std::array<std::byte,flare::mission::signature.size()> code{};
        for(std::size_t i=0;i<code.size();++i)code[i]=std::byte(flare::mission::signature[i]);
        const auto set=[&](unsigned at,unsigned value){std::memcpy(code.data()+at,&value,4);};
        for(auto at:{9u,21u,205u})set(at,1);
        for(auto at:{92u,109u})set(at,2);
        for(auto at:{97u,188u})set(at,3);set(72,4);
        check(flare::mission::locate(code,1,2,3,4)==0,"complete native instruction witness admits exactly the flare sequence");
        check(!flare::mission::locate(std::span(code).first(code.size()-1),1,2,3,4),"truncated mission bytecode cannot install partial hooks");
        for(std::size_t i=0;i<code.size();++i)if(flare::mission::mask[i])
        {code[i]^=std::byte{1};check(!flare::mission::locate(code,1,2,3,4),"changed native opcode rejects the whole adapter");code[i]^=std::byte{1};}
        set(92,5);check(!flare::mission::locate(code,1,2,3,4),"unrelated fire waiter cannot authorize this adapter");set(92,2);
        std::array<std::byte,2*code.size()> ambiguous{};std::copy(code.begin(),code.end(),ambiguous.begin());std::copy(code.begin(),code.end(),ambiguous.begin()+code.size());
        check(!flare::mission::locate(ambiguous,1,2,3,4),"duplicate native blocks are rejected instead of choosing arbitrary control flow");
    }
    check(vr::hud_prompts::resolve(vr::hud_prompts::source::script_hud,"SCRIPT_PLATFORM_HINTSTR_POPFLARE",{true,"dc_whitehouse",vr::hud_prompts::feature::signal_flare})==game_text::key::signal_flare &&
        !vr::hud_prompts::resolve(vr::hud_prompts::source::script_hud,"SCRIPT_PLATFORM_HINTSTR_POPFLARE_OTHER",{true,"dc_whitehouse",vr::hud_prompts::feature::signal_flare}),"flare prompt replacement requires the exact native localization identity");
    check(game_text::format(game_text::key::signal_flare,game_text::locale::simplified_chinese)==
        game_text::utf8(u8"从装备位取出信号弹，移除盖子点燃"),"Chinese flare instruction preserves the approved UTF-8 sentence");
    {
        namespace hi=hand_interaction;hi::arbiter a;a.begin(1,1);
        const hi::grasp tube{{hi::domain::special,{65,1},20},hi::role::control,hi::button::grip,hi::recipe::single,hi::capability::action};
        a.offer({hand::left,tube,1,20,.2f,1,true,true});a.offer({hand::right,tube,1,20,.3f,1,true,true});
        a.resolve([](const auto&){return true;});
        check(a.find(hand::left,tube.destination) && !a.find(hand::right,tube.destination),"two simultaneous belt grabs cannot duplicate the flare");
        auto cap=tube;cap.destination.component=21;cap.purpose=hi::role::part;
        a.begin(2,1);a.offer({hand::left,cap,2,10,.1f,1,true,true});a.offer({hand::right,cap,2,10,.2f,1,true,true});
        a.resolve([](const auto&){return true;});
        check(!a.find(hand::left,cap.destination) && a.find(hand::right,cap.destination),"only the other free hand can own the cap");
        for(auto h:{hand::left,hand::right})
        {const auto plan=hi::compose_pose(h,a.sessions());check(!plan.melee && !hi::has(plan.abilities,hi::capability::fire),"tube and cap leases suppress weapon fire and fist melee in their hands");}
    }
    for(auto control:{hand_interaction::button::grip,hand_interaction::button::trigger})
    {
        namespace hi=hand_interaction;hi::arbiter a;a.begin(1,1);
        const hi::grasp tube{{hi::domain::special,{65,1},20},hi::role::control,hi::button::grip,hi::recipe::single,hi::capability::action};
        auto cap=tube;cap.destination.component=21;cap.purpose=hi::role::part;cap.maintained=control;
        a.offer({hand::left,tube,1,20,.1f,1,true,true});a.resolve([](const auto&){return true;});
        a.begin(2,1);a.offer({hand::right,cap,2,10,.1f,1,true,true});a.resolve([](const auto&){return true;});
        const auto* lease=a.find(hand::right,cap.destination);
        check(lease && lease->held.maintained==control,"cap arbiter preserves the actual trigger or grip that acquired it");
        const auto plan=hi::compose_pose(hand::right,a.sessions());
        check(!plan.melee && !hi::has(plan.abilities,hi::capability::fire),"either cap button retains action-only ownership without firing a weapon");
    }
}
