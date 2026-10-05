#include <std_include.hpp>
#include "ladder_runtime.hpp"
#include "ladder_scene.hpp"
#include "ladder_native.hpp"
#include "climb_collision.hpp"
#include "hand_position_offset.hpp"
#include "empty_hand_pose.hpp"
#include "native_hand_schema.hpp"
#include "native_carry.hpp"
#include "native_scripted_control.hpp"
#include "cliffhanger_physical.hpp"
#include "hand_interaction/runtime.hpp"
#include "game/scripting/execution.hpp"
#include "component/scripting.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/native_memory.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::ladders
{
    namespace
    {
        namespace hi=hand_interaction;
        using clock=controller_input::clock;
        constexpr int player_mask=0x2810011;
        constexpr std::string_view carrier_name="vr_ladder_carrier";
        enum class phase{idle,pulling,lifting,landing};
        phase stage{};pull_solver solver;
        std::array<scene::contact,2> held{},offered{};
        std::array<vec,2> motions{};controller_input::frame previous;
        std::uint64_t reference{},continuity{},sequence{};int last_time{};
        weapons::native_carry::world_key carrier{};
        // Native fields do not retain a script object. Keep our owned entity
        // referenced until deletion so linked-camera identity stays stable.
        std::optional<scripting::entity> carrier_script;
        float units{},top_height{},top_start{};bool top_armed{},verified{},running{true};
        exit_route exit;vec exit_normal{};
        game::Bounds standing_bounds{};bool standing_valid{};
        vec body_position{},carrier_offset{};bool body_ready{};
        struct preview_sample
        {
            head_pose_bridge::spatial_frame space{};position_offsets offsets{};
            std::array<anchor,2> wrists{};pull_window window{};
            std::uint64_t continuity{};bool valid{};
        } preview;
        std::string reason="waiting for ladder contact";
        unsigned moves{},falls{},exits{},obstructions{};
        struct presentation
        {unsigned held{};std::array<scene::contact,2> contacts{};vec body{};float units{};std::uint64_t reference{};clock::time_point at{};bool moving{};int carrier{-1};preview_sample preview{};};
        std::mutex mutex;presentation published;
        struct grip_binding
        {std::array<bar_grip::binding,2> bars{};std::array<quat,2> basis{};std::uint64_t reference{};} grips;
        camera_translation camera_position;
        std::uint64_t camera_sequence{};vec camera_body{};
        int camera_time{-1};
        std::atomic_uint64_t camera_samples{};std::atomic_uint bar_pose_hands{};
        template<class T>T read(const void* p,size_t at)
        {T v{};if(p)utils::native_memory::read_bytes(&v,static_cast<const std::byte*>(p)+at,sizeof(v));return v;}
        scripting::entity entity(int n){return scripting::entity{game::scr_entref_t{static_cast<unsigned short>(n),0}};}
        scripting::script_value vector(vec p){return scripting::vector{p[0],p[1],p[2]};}
        int parent()
        {
            const auto* tag=read<const void*>(&game::g_entities[0],0x208);const auto* p=read<const game::gentity_s*>(tag,0);
            if(!p)return -1;
            const auto address=reinterpret_cast<uintptr_t>(p),base=reinterpret_cast<uintptr_t>(game::g_entities.get());
            if(address<base || (address-base)%sizeof(game::gentity_s))return -1;
            const auto n=(address-base)/sizeof(game::gentity_s);return n<game::ENTITYNUM_WORLD?int(n):-1;
        }
        vec position(){return body_ready?body_position:read<vec>(game::g_entities[0].client,0x80);}
        bool configured()
        {
            const auto* vr=game::Dvar_FindVar("vr_enable");const auto* hands=game::Dvar_FindVar("vr_independentHands");
            return running && verified && native::enabled() && vr && vr->current.enabled && hands && hands->current.enabled && game::CL_IsCgameInitialized();
        }
        bool live_carrier(){return carrier.entity>0 && weapons::native_carry::entity_key(carrier.entity)==carrier;}
        hi::grasp grasp(const scene::contact& c)
        {return {hi::object(hi::domain::ladder,c.object,c.edge),hi::role::part,hi::button::grip,hi::recipe::single,hi::capability::action};}
        void publish()
        {
            presentation p;p.reference=reference;p.at=clock::now();p.moving=stage!=phase::idle;p.carrier=carrier.entity;
            p.body=body_position;p.units=units;p.contacts=held;p.preview=preview;
            for(unsigned h=0;h<2;++h)if(held[h])
            {
                p.held|=1u<<h;
            }
            const std::lock_guard lock(mutex);published=p;if(!p.moving){camera_position.reset();camera_sequence=0;camera_time=-1;}
        }
        void forget()
        {stage=phase::idle;held={};offered={};solver.clear();previous={};motions={};carrier_script.reset();carrier={};sequence=reference=continuity=0;last_time=0;top_armed=false;standing_valid=body_ready=false;exit={};preview={};publish();}
        bool bounds(game::Bounds& b);
        free_climb::sweep_hit sweep(vec from,vec to,const game::Bounds& b);
        bool grounded()
        {
            game::Bounds b{};if(!bounds(b))return false;
            const auto from=position(),to=add(from,vec{0,0,-.035f*units});
            const auto hit=sweep(from,to,b);
            return !hit.startsolid && !hit.allsolid && std::isfinite(hit.fraction) && hit.fraction>=0 && hit.fraction<1 && hit.normal[2]>=.7f;
        }
        void detach(bool falling)
        {
            // Never unlink a newer story/vehicle owner or delete a reused slot.
            const bool ours=live_carrier();const int owner=parent();
            if(ours && owner==carrier.entity)
            {
                const bool ground=grounded();
                auto player=entity(0);player.call("playersetgroundreferenceent",{scripting::script_value{}});player.call("unlink");
                if(body_ready)player.call("setorigin",{vector(body_position)});
                if(falling)
                {
                    vec normal=exit_normal;
                    for(const auto& c:held)if(c){normal=c.normal;break;}
                    const auto velocity=ground?vec{}:add(scale(normal,.6f*units),vec{0,0,-.15f*units});player.call("setvelocity",{vector(velocity)});
                    // Witnessed native ladder-jump state: release ladder support
                    // and prevent immediate automatic recatch on the same face.
                    auto* ps=reinterpret_cast<std::byte*>(game::g_entities[0].client);
                    if(ps){auto flags=read<unsigned>(ps,0x54);flags&=~unsigned(game::PMF_LADDER);if(!ground)flags|=0x1000u;std::memcpy(ps+0x54,&flags,4);}
                    if(!ground)++falls;else reason="bottom ground support; returned to standing";
                }
            }
            if(ours)entity(carrier.entity).call("delete");
            forget();
        }
        bool attach_carrier()
        {
            if(parent()>=0)return false;
            if(!bounds(standing_bounds))return false;standing_valid=true;
            const auto start=position();
            auto proxy=scripting::call("spawn",{"script_model",vector(start)}).as<scripting::entity>();
            carrier=weapons::native_carry::entity_key(proxy.get_entity_reference().entnum);
            carrier_script=proxy;
            try
            {
                proxy.call("setmodel",{"tag_origin"});proxy.set("targetname",std::string(carrier_name));proxy.call("hide");proxy.call("notsolid");
                // Native link arguments 3/4 are yaw bounds, 5/6 are pitch.
                // Zero bounds clamp the native command heading and cancel HMD
                // yaw when normal gameplay composition removes its contribution.
                entity(0).call("playerlinktodelta",{proxy,"tag_origin",1,180,180,85,85,1});entity(0).call("playersetgroundreferenceent",{proxy});
                if(parent()!=carrier.entity){detach(false);return false;}
                carrier_offset=sub(read<vec>(&game::g_entities[carrier.entity],0x1c),start);
                body_position=start;body_ready=true;stage=phase::pulling;return true;
            }
            catch(...){if(live_carrier()){if(parent()==carrier.entity)entity(0).call("unlink");proxy.call("delete");}carrier_script.reset();carrier={};throw;}
        }
        bool bounds(game::Bounds& b)
        {
            // Retain the pre-link full body/feet shape. A linked or animated
            // body must not shrink the downward sweep or let feet enter floors.
            b=standing_valid?standing_bounds:read<game::Bounds>(&game::g_entities[0],0xc0);
            for(unsigned j=0;j<3;++j)if(!std::isfinite(b.midPoint[j]) || !std::isfinite(b.halfSize[j]) || b.halfSize[j]<=0 || b.halfSize[j]>128)return false;
            return true;
        }
        free_climb::sweep_hit sweep(vec from,vec to,const game::Bounds& b)
        {
            game::trace_t t{};game::G_TraceCapsule(&t,from.data(),to.data(),&b,0,player_mask);
            return {t.fraction,{t.normal[0],t.normal[1],t.normal[2]},bool(t.startsolid),bool(t.allsolid)};
        }
        bool move(vec desired)
        {
            const auto actual=position();game::Bounds b{};if(!bounds(b))return false;
            const auto hit=free_climb::slide(actual,sub(desired,actual),.003f*units,[&](vec a,vec z){return sweep(a,z,b);});
            if(hit.blocked){++obstructions;solver.obstructed(scale(hit.position,1/units));}
            if(hit.stuck)return false;
            // A carrier can already be ahead of the still-unsynchronized PS.
            // Commit the absolute accepted body position once, rather than
            // adding a delta computed from that delayed PS onto the carrier.
            const auto target=add(hit.position,carrier_offset);
            if(length(sub(hit.position,actual))>.001f){entity(carrier.entity).set("origin",vector(target));++moves;}
            body_position=hit.position;
            return true;
        }
        pull_window preview_limits(vec motion)
        {
            game::Bounds b{};if(!bounds(b))return {};
            const auto p=position();const float reach=.12f*units,skin=.003f*units;
            const auto direction=length(motion)>.001f?unit(motion):vec{0,0,1};
            const auto limit=[&](float sign) {
                const auto hit=sweep(p,add(p,scale(direction,sign*reach)),b);
                if(hit.startsolid || hit.allsolid || !std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1)return 0.f;
                return std::max(0.f,reach*hit.fraction-(hit.fraction<1?skin:0.f));
            };
            return {direction,-limit(-1),limit(1),true};
        }
        exit_route landing(const scene::contact& support)
        {
            game::Bounds b{};if(!bounds(b))return {};
            const auto actual=position();auto center=scale(add(actual,support.point),.5f);center[2]=top_height;
            // Search above and beyond the face, keeping the full player capsule.
            for(float advance:{.4f,.65f,.9f})
            {
                auto from=sub(center,scale(support.normal,advance*units));from[2]=top_height+1.2f*units;
                auto to=from;to[2]=top_height-.2f*units;game::trace_t floor{};
                game::G_TraceCapsule(&floor,from.data(),to.data(),&b,0,player_mask);
                if(floor.startsolid || floor.allsolid || !std::isfinite(floor.fraction) || floor.fraction>=1 || floor.normal[2]<.7f)continue;
                const auto ground=add(add(from,scale(sub(to,from),floor.fraction)),vec{0,0,.01f*units});
                exit_route route;route.landing=ground;route.lift=actual;route.lift[2]=std::max(ground[2]+.08f*units,actual[2]);route.valid=true;
                if(clear_route(actual,route,[&](vec a,vec z){return sweep(a,z,b);}))return route;
            }
            return {};
        }
        bool permission()
        {
            const auto* ps=game::g_entities[0].client;
            return configured() && ps && scripted_control::allowed(ps) && !cliffhanger_physical::owns_movement() &&
                !(read<unsigned>(ps,0x54)&(game::PMF_PRONE|game::PMF_DUCKED|game::PMF_MANTLE));
        }
    }
    bool owns_movement()noexcept{const std::lock_guard lock(mutex);return published.moving;}
    bool owns_carrier(int ent)noexcept{const std::lock_guard lock(mutex);return published.moving && published.carrier==ent;}
    void collect_interactions(const hi::frame& f)noexcept
    {
        offered={};
        try
        {
            if(!permission() || (stage==phase::idle && parent()>=0))return;
            // Held contacts are refreshed directly. Discovery and mesh reads
            // are acquisition work, not a recurring cost on every climb step.
            if(!solver.held())scene::prepare(position(),f.body.units_per_meter);
            if(stage==phase::lifting || stage==phase::landing)return;
            grip_binding binding;{const std::lock_guard lock(mutex);binding=grips;}
            if(binding.reference!=f.input.reference_generation)return;
            for(unsigned h=0;h<2;++h)
            {
                if(held[h] || !(f.valid_hands&(1u<<h)) || !hi::free(hand(h)))continue;
                const auto edge=hi::input(hand(h),hi::button::grip);if(!edge.press || !edge.down || edge.release)continue;
                if(!binding.bars[h].valid)continue;
                const auto palm=bar_grip::centre(binding.bars[h],f.wrists[h],binding.basis[h]);
                const auto c=scene::nearest(palm,f.body.head_position,f.body.units_per_meter);
                if(!c)continue;
                const auto existing=held[0]?held[0]:held[1];
                if(existing && (!same_column(c.point,existing.point,existing.normal,f.body.units_per_meter) || dot(c.normal,existing.normal)<.8f))continue;
                offered[h]=c;hi::offer({hand(h),grasp(c),edge.event,5,c.distance/f.body.units_per_meter,1,true,false});
            }
        }
        catch(const std::exception& e){reason=e.what();}
    }
    void update_interactions()noexcept
    {
        const auto* f=hi::simulation();if(!f)return;
        try
        {
            if(stage!=phase::idle && (!permission() || !live_carrier() || parent()!=carrier.entity)){reason="native ownership changed";detach(false);return;}
            if(!permission())return;
            const float new_units=f->body.units_per_meter;
            if(!std::isfinite(new_units) || new_units<=0 || new_units>10000)return;
            if(stage!=phase::idle && units!=new_units){reason="world scale changed";detach(true);return;}
            units=new_units;
            const auto old_mask=solver.held();
            for(unsigned h=0;h<2;++h)if(held[h])
            {
                const auto edge=hi::input(hand(h),hi::button::grip);
                if(edge.release || (edge.armed && !edge.down) || !scene::refresh(held[h],units))
                {exit_normal=held[h].normal;held[h]={};solver.release(h);}
            }
            // A last-hand release wins over any same-frame new acquisition.
            if(old_mask && !solver.held()){reason="last grip released; native fall";detach(true);return;}
            for(unsigned h=0;h<2;++h)if(offered[h] && hi::granted(hand(h),hi::domain::ladder,offered[h].object,hi::button::grip))
            {
                if(!scene::refresh(offered[h],units))continue;
                if(stage==phase::idle && !attach_carrier()){reason="native link rejected";continue;}
                held[h]=offered[h];solver.attach(h,scale(held[h].point,1/units),held[h].normal,f->wrists[h].rotation);
                const float next_top=scene::top(held[h],units);
                if(std::abs(next_top-top_height)>.04f*units)top_armed=false;
                top_height=next_top;reason="physical ladder grip";
            }
            if(stage==phase::idle)return;
            if(old_mask!=solver.held())solver.topology_changed(scale(position(),1/units));
            if(f->input.sequence==sequence){publish();return;}
            const int time=read<int>(game::g_entities[0].client,0x4c);
            if(last_time && time<last_time){reason="checkpoint changed";detach(false);return;}
            float dt=previous.sequence?std::chrono::duration<float>(f->input.sampled_at-previous.sampled_at).count():.016f;
            const bool continuous=reference==f->input.reference_generation && continuity==f->input.continuity_generation && previous.sequence && dt>0 && dt<=.15f;
            if(!continuous){solver.rebase();previous={};preview={};top_armed=false;dt=.016f;}
            reference=f->input.reference_generation;continuity=f->input.continuity_generation;sequence=f->input.sequence;last_time=time;
            std::array<free_climb::hand,2> input{};
            position_offsets offsets;
            if(const auto* x=game::Dvar_FindVar("vr_handOffsetInward"))offsets.inward_meters=x->current.value;
            if(const auto* x=game::Dvar_FindVar("vr_handOffsetBack"))offsets.back_meters=x->current.value;
            if(const auto* x=game::Dvar_FindVar("vr_handOffsetUp"))offsets.up_meters=x->current.value;
            bool valid=true;
            std::array<anchor,2> current_wrists{};
            for(unsigned h=0;h<2;++h)if(held[h])
            {
                anchor wrist,prior;
                if(!(f->valid_hands&(1u<<h)) || !tracked_wrist(f->input,f->body,{},int(h),offsets,wrist,true)){valid=false;continue;}
                if(previous.sequence && tracked_wrist(previous,f->body,{},int(h),offsets,prior,true))
                    motions[h]=add(motions[h],scale(sub(wrist.position,prior.position),1/units));
                else motions[h]=scale(wrist.position,1/units);
                current_wrists[h]=wrist;
                input[h]={motions[h],scale(wrist.position,1/units),wrist.rotation,true,true,motions[h]};
            }
            if(!valid){solver.rebase();previous={};preview={};reason="tracking lost; grip retained";publish();return;}
            previous=f->input;
            const auto actual=position();vec pull_direction{};
            if(stage==phase::pulling)
            {
                const auto goal=solver.update(scale(actual,1/units),reference,dt,input);
                pull_direction=sub(scale(goal,units),actual);
                if(!move(scale(free_climb::bounded_step(scale(actual,1/units),goal,dt,2.f),units)))solver.obstructed(scale(position(),1/units));
                scene::contact top_contact;
                for(auto c:held)if(c && c.point[2]>=top_height-.04f*units)top_contact=c;
                if(top_contact)
                {
                    if(!top_armed){top_armed=true;top_start=actual[2];}
                    if(goal[2]*units>top_start+top_pull_meters*units)
                    {
                        exit=landing(top_contact);
                        if(exit.valid){stage=phase::lifting;reason="clear top landing; lifting";}
                        else reason="top landing blocked; grip retained";
                    }
                }
                else top_armed=false;
            }
            else
            {
                const auto target=stage==phase::lifting?exit.lift:exit.landing;
                const float speed=stage==phase::lifting?4.5f:1.5f;
                if(!move(scale(free_climb::bounded_step(scale(actual,1/units),scale(target,1/units),dt,speed),units)))
                {stage=phase::pulling;top_armed=false;solver.rebase();reason="exit obstructed; grip retained";}
                else if(length(sub(actual,target))<.025f*units)
                {
                    if(stage==phase::lifting){stage=phase::landing;reason="moving onto top ground";}
                    else{++exits;reason="standing on top ground";detach(false);return;}
                }
            }
            preview={};
            if(stage==phase::pulling)
                preview={f->body,offsets,current_wrists,preview_limits(pull_direction),f->input.continuity_generation,true};
            publish();
        }
        catch(const std::exception& e)
        {reason=e.what();try{detach(false);}catch(...){forget();}}
    }
    void report_interactions()noexcept
    {for(unsigned h=0;h<2;++h)if(held[h])hi::observed(hand(h),grasp(held[h]));}
    void lifecycle(bool suspended)noexcept
    {
        try
        {
            if(stage==phase::idle)
            {
                // A save may contain our native link but no live input/grasp.
                // Recover by releasing only our named carrier, never story rigs.
                if(running && verified && game::CL_IsCgameInitialized() && parent()>0)
                {
                    auto proxy=entity(parent());
                    if(proxy.get("targetname").is<std::string>() && proxy.get("targetname").as<std::string>()==carrier_name)
                    {
                        carrier=weapons::native_carry::entity_key(parent());reason="restored ladder carrier released";detach(false);
                    }
                }
                return;
            }
            if(!configured() || !permission() || !live_carrier() || parent()!=carrier.entity){detach(false);return;}
            if(suspended){solver.rebase();previous={};preview={};reason="input suspended; native carrier held";publish();}
        }
        catch(...){try{detach(false);}catch(...){forget();}}
    }
    bool apply_camera_origin(float* origin,float view_height,int linked_entity,int frame_time)noexcept
    {
        if(!origin || !std::isfinite(view_height) || view_height<0 || view_height>128)return false;
        const auto input=controller_input::latest();
        const auto now=clock::now();const std::lock_guard lock(mutex);
        if(!published.moving || published.carrier!=linked_entity || !published.reference || input.reference_generation!=published.reference || !input.sequence || now-published.at>150ms)return false;
        const auto seconds=std::chrono::duration<double>(now.time_since_epoch()).count();
        if(camera_time!=frame_time || !camera_sequence)
        {
            auto desired=published.body;
            const auto& baseline=published.preview;
            if(baseline.valid && input.focused && !input.orientation_settling && input.continuity_generation==baseline.continuity &&
                now>=input.sampled_at && now-input.sampled_at<=150ms)
            {
                std::array<vec,2> delta{};bool valid=true;unsigned active_hands{};
                for(unsigned h=0;h<2;++h)if((published.held&(1u<<h)) && input.squeeze[h].active && input.squeeze[h].down)active_hands|=1u<<h;
                for(unsigned h=0;h<2;++h)if(active_hands&(1u<<h))
                {
                    anchor wrist;
                    if(!tracked_wrist(input,baseline.space,{},int(h),baseline.offsets,wrist,true)){valid=false;break;}
                    delta[h]=sub(wrist.position,baseline.wrists[h].position);
                }
                if(valid)desired=preview_pull(desired,delta,active_hands,baseline.window);
            }
            camera_body=camera_position.sample(desired,published.reference,seconds,published.units);camera_sequence=input.sequence;camera_time=frame_time;++camera_samples;
        }
        auto p=camera_body;p[2]+=view_height;
        std::copy(p.begin(),p.end(),origin);return true;
    }
    void present(const interaction_rig& parts,const rig& r,const controller_input::frame& input,
        const std::array<vec,2>& shoulders,const std::array<vec,3>& axes,vec offset,std::span<bone> solved,unsigned visible)noexcept
    {
        const unsigned bound=unsigned(parts.bar_grips[0].valid)|(unsigned(parts.bar_grips[1].valid)<<1);
        bar_pose_hands=bound;
        presentation p;{const std::lock_guard lock(mutex);p=published;
            // Model-local anatomy is shared with admission, including on the
            // weapon-model path. No rendered world pose feeds the pull solver.
            grips={parts.bar_grips,parts.basis,input.reference_generation};}
        if(p.reference!=input.reference_generation || clock::now()-p.at>150ms)return;
        const auto held=p.held&visible&bound;
        std::array<anchor,2> contacts{};
        for(unsigned h=0;h<2;++h)if(held&(1u<<h))
        {
            contacts[h]=bar_grip::on_bar(parts.bar_grips[h],p.contacts[h].point,p.contacts[h].normal,h);
            contacts[h].position=sub(contacts[h].position,offset);
        }
        if(!bar_grip::constrain(r,solved,contacts,held,shoulders,axes))return;
        for(unsigned h=0;h<2;++h)if(held&(1u<<h))
            empty_hand::apply(r,parts.library,native_hand_schema::definition,hand(h),{true,empty_hand::gesture::fist,bar_grip::wrap},solved);
    }
    std::string status()
    {return std::format("verified={} phase={} hands={} carrier={} moves={} falls={} exits={} blocked={} top={} reason={}\nbody=({}, {}, {}) bar_pose_hands={} camera_samples={}\n{}\n",verified,int(stage),solver.held(),carrier.entity,moves,falls,exits,obstructions,top_height,reason,body_position[0],body_position[1],body_position[2],bar_pose_hands.load(),camera_samples.load(),scene::status());}
    class component final:public component_interface
    {
        void post_unpack()override
        {
            constexpr std::uint8_t bytes[]{0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x78};
            std::array<std::uint8_t,sizeof(bytes)> mask{};mask.fill(255);
            // The collision wrapper is shared with existing native consumers;
            // validate its executable mapping without installing another hook.
            verified=bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1404CBFE0),{bytes,mask.data(),sizeof(bytes)}));
            if(verified)verified=native::initialize();
            scripting::on_level_start([]{forget();scene::reset();});
            scripting::on_shutdown([](bool,bool after){if(!after){try{detach(false);}catch(...){forget();}scene::reset();}});
            command::add("vr_ladder_status",[]{scheduler::once([]{const auto text=status();console::info("[VR ladder] %s",text.c_str());scheduler::once([text]{utils::io::write_file_atomic("minidumps/h2-mod-vr-ladder.txt",text);},scheduler::pipeline::async);},scheduler::pipeline::server);});
        }
        void pre_destroy()override{running=false;}
    };
}
REGISTER_COMPONENT(vr::gameplay::ladders::component)
