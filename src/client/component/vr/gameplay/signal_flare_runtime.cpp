#include <std_include.hpp>
#include "signal_flare_runtime.hpp"
#include "signal_flare_policy.hpp"
#include "signal_flare_profile.hpp"
#include "signal_flare_mission.hpp"
#include "abdominal_interaction.hpp"
#include "hand_attachment_pose.hpp"
#include "hand_interaction/runtime.hpp"
#include "part_hand_constraint.hpp"
#include "native_scripted_control.hpp"
#include "native_ammunition.hpp"
#include "grenade_state.hpp"
#include "reload_item.hpp"
#include "weapon_feedback.hpp"
#include "native_weapon_sound.hpp"
#include "native_weapon_fx.hpp"
#include "native_followed_fx.hpp"
#include "../engine_stereo_view.hpp"
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::equipment::special::flare
{
    namespace
    {
        using namespace hands;using namespace hands::pose_math;
        namespace hi=hand_interaction;using clock=controller_input::clock;
        struct resources
        {
            game::XModel* source{};std::array<scene_models::rigid_part,2> pieces;
            std::array<anchor,2> bind{}; // Source-model bone frames, not mesh-local vertices.
            anchor cap{},tip{};vec center{},cap_center{};float half_height{};
            native_followed_fx::handle burning;game::FxEffectDef* ignition{};
        };
        struct snapshot
        {
            state value;std::shared_ptr<resources> asset;
            anchor root{},cap_root{},cap_grasp{};hand cap_hand{hand::none};std::uint64_t cap_revision{};
            bool enabled{},hands_ready{},cap_visible{};unsigned weapon{};
            std::array<quat,2> basis{},mirror{};
            std::uint64_t reference{},epoch{};clock::time_point at{};
            std::uint64_t ignition{};int lit_at{};clock::time_point cap_grasp_at{};
        };
        std::mutex mutex;snapshot published,submitted;
        std::shared_ptr<resources> asset;game::XModel* attempted{};
        state held;anchor root{},cap_root{},cap_grasp{};hand cap_hand{hand::none};std::uint64_t cap_revision{};
        reload_items::flight body_flight{},cap_flight{};bool body_rest{},cap_falling{},cap_visible{};
        grenades::release_motion motion;grab_intent intent,trigger_intent;
        hi::button cap_button{hi::button::grip}; // Latched through pull and detached-cap ownership.
        std::array<unsigned short,2> lighting{};
        unsigned weapon{};bool enabled{};std::uint64_t epoch{},timeline{},reference{};
        clock::time_point sample_at{},cap_grasp_at{};int lit_at{},last_time{};
        std::uint64_t ignition{},next_ignition{},played_ignition{};
        std::atomic_uint64_t takes{},handoffs{},pulls{},fx_failed{},sounds_played{},sounds_failed{};
        std::atomic<const char*> reason{"waiting for Whisky Hotel"};
        bool fresh(clock::time_point at){const auto now=clock::now();return now>=at && now-at<=150ms;}
        anchor bind(const game::DObjAnimMat& b){return {{b.trans[0],b.trans[1],b.trans[2]},normalize({b.quat[0],b.quat[1],b.quat[2],b.quat[3]})};}
        anchor wrist(const anchor& raw,const snapshot& s,unsigned h){return {raw.position,normalize(multiply(raw.rotation,s.basis[h]))};}
        anchor held_root(const std::array<anchor,2>& wrists,const snapshot& s)
        {const auto h=unsigned(held.holder);return compose(wrist(wrists[h],s,h),authored::attachment(h,s.mirror[h]));}
        hi::target target(unsigned part=0,std::uint64_t revision=0)
        {return hi::object(hi::domain::special,{weapon,revision?revision:held.revision},20+part);}
        void publish(const hi::frame* f=nullptr)
        {
            const std::lock_guard lock(mutex);published.value=held;published.root=root;published.cap_root=cap_root;
            published.cap_grasp=cap_grasp;
            published.cap_hand=cap_hand;published.cap_revision=cap_revision;published.cap_visible=cap_visible;
            published.weapon=weapon;published.enabled=enabled;published.epoch=epoch;
            published.ignition=ignition;published.lit_at=lit_at;published.cap_grasp_at=cap_grasp_at;
            if(f){published.reference=f->input.reference_generation;published.at=f->input.sampled_at;}
        }
        void reset()
        {
            held.reset(mission::consumed());intent.reset();trigger_intent.reset();motion.reset();cap_hand=hand::none;++cap_revision;
            cap_button=hi::button::grip;
            body_flight={};cap_flight={};body_rest=cap_falling=cap_visible=false;lit_at=last_time=0;ignition=0;cap_grasp_at={};
            if(asset)native_followed_fx::stop(asset->burning);
            timeline=weapons::native_ammunition::timeline();epoch=mission::generation();
        }
        void refresh()
        {
            if(!mission::active() || asset || !game::CL_IsCgameInitialized() || !scene_models::ready())return;
            auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,"h2_viewmodel_flare",0).model;
            if(!source || !source->name || std::string_view(source->name)!="h2_viewmodel_flare" || source==attempted)return;
            attempted=source;
            if(source->numBones!=5 || source->numLods!=1 || source->numsurfs!=1 || !source->boneNames || !source->baseMat)return;
            constexpr std::array names{"j_gun","j_flare","j_striker_cap","tag_fire_fx","tag_flash"};
            for(unsigned i=0;i<names.size();++i){const auto* name=game::SL_ConvertToString(source->boneNames[i]);if(!name || std::string_view(name)!=names[i])return;}
            auto* burn=game::DB_FindXAssetHeader(game::ASSET_TYPE_FX,"fx/misc/handflare_green_view",0).fx;
            auto* ignite=game::DB_FindXAssetHeader(game::ASSET_TYPE_FX,"fx/misc/handflare_green_ignite",0).fx;
            if(!burn || !burn->name || std::string_view(burn->name)!="fx/misc/handflare_green_view" ||
                burn->msecLoopingLife!=burn_msec || burn->elemDefCountLooping!=13 || burn->elemDefCountEmission || burn->elemDefCountOneShot ||
                !ignite || !ignite->name || std::string_view(ignite->name)!="fx/misc/handflare_green_ignite" ||
                ignite->msecLoopingLife || ignite->elemDefCountLooping || ignite->elemDefCountEmission || ignite->elemDefCountOneShot!=7)
            {reason="native flare FX contract rejected";return;}
            auto next=std::make_shared<resources>();next->source=source;
            if(!next->pieces[0].create(source,1) || !next->pieces[1].create(source,2)){reason="flare mesh partition rejected";return;}
            for(unsigned p=0;p<2;++p)next->bind[p]=bind(source->baseMat[p+1]);
            const auto source_to_tube=inverse(next->bind[0]);
            next->cap=compose(source_to_tube,next->bind[1]);
            next->tip=compose(source_to_tube,bind(source->baseMat[3]));
            next->center=compose(source_to_tube,{{source->bounds.midPoint[0],source->bounds.midPoint[1],source->bounds.midPoint[2]},{0,0,0,1}}).position;
            // Partition bounds, like its vertices, remain in SOURCE-model
            // space. Convert directly into the tube frame for cap acquisition.
            const auto& b=next->pieces[1].model()->bounds;
            next->cap_center=compose(source_to_tube,{{b.midPoint[0],b.midPoint[1],b.midPoint[2]},{0,0,0,1}}).position;
            for(unsigned i=0;i<3;++i)
            {
                vec extent{};extent[i]=source->bounds.halfSize[i];
                next->half_height+=std::abs(rotate(source_to_tube.rotation,extent)[1]);
            }
            // Core flame/light stays on the moving emitter; emitted smoke and
            // sparks retain their native world/offset behavior and lifetimes.
            constexpr std::array<unsigned,8> attached{0,1,2,3,4,5,6,7};
            next->burning=native_followed_fx::create(burn,attached);next->ignition=ignite;
            if(!next->burning){reason="native following flare FX unavailable";return;}
            if(!scene_models::register_runtime_models(std::array<scene_models::runtime_model,2>{{{next->pieces[0].model(),source},{next->pieces[1].model(),source}}}))return;
            asset=next;{const std::lock_guard lock(mutex);published.asset=next;}reason="native tube and removable cap ready";
        }
        bool visible(const snapshot& s,unsigned part)
        {
            if(!s.enabled || !s.asset || !fresh(s.at))return false;
            return part==0?s.value.stage!=phase::spent:(!s.value.cap_removed?s.value.stage!=phase::spent:s.cap_visible);
        }
        anchor attached_cap(const snapshot& s,const anchor& base)
        {auto local=s.asset->cap;local.position[2]+=s.value.travel;return compose(base,local);}
        void feedback()
        {
            snapshot s;{const std::lock_guard lock(mutex);s=published;}
            if(!s.enabled || !s.asset || !s.value.lit || !s.ignition || !fresh(s.at) || !game::CL_IsCgameInitialized())return;
            if(!burning(s.value,game::CG_GetGameTime(0),s.lit_at)){native_followed_fx::stop(s.asset->burning);return;}
            if(s.ignition==played_ignition)return;played_ignition=s.ignition;
            const auto tip=compose(s.root,s.asset->tip);
            if(!native_followed_fx::start(s.asset->burning,s.ignition,s.lit_at,tip))++fx_failed;
            if(!weapons::native_weapon_fx::play_frontend(s.asset->ignition,tip,s.lit_at))++fx_failed;
            // Whole original pop/ignite/burn recording, exactly once after the
            // physical/native transaction succeeds. No animation is replayed.
            if(weapons::native_weapon_sound::play_prop(s.weapon,"flare","scn_dcemp_road_flare_pop_wave",tip.position))++sounds_played;
            else ++sounds_failed;
        }
        void submit()
        {
            snapshot s;{const std::lock_guard lock(mutex);s=published;submitted=s;}
            if(!mission::active() || !scripted_control::predicted_allowed())return;
            auto base=s.root;
            if(s.value.stage==phase::stowed && s.asset)
            {head_pose_bridge::spatial_frame body;if(!head_pose_bridge::get_spatial_frame(body) || body.generation!=s.reference)return;base=stowed(body,s.asset->center,s.asset->half_height);}
            for(unsigned p=0;p<2;++p)if(visible(s,p))
            {
                const auto bone=p?(s.value.cap_removed?s.cap_root:attached_cap(s,base)):base;
                const auto at=rigid_delta(bone,s.asset->bind[p]);
                game::GfxScaledPlacement placement{};placement.scale=1;
                std::copy(at.position.begin(),at.position.end(),placement.base.origin);std::copy(at.rotation.begin(),at.rotation.end(),placement.base.quat);
                // The tube and detached cap can leave the player independently.
                const bool carried=p && s.value.cap_removed?vr::valid_hand(s.cap_hand):
                    s.value.stage==phase::stowed || s.value.held();
                float white[]{1,1,1,1};scene_models::submit(s.asset->pieces[p].model(),&placement,
                    carried?scene_models::no_cast_shadow:0u,&lighting[p],white,white,white,8.f);
            }
        }
        scene_models::placement_result prepare(const scene_models::preparation& p,const void* entry,game::GfxPlacement& current,game::GfxPlacement& previous)noexcept
        {
            using result=scene_models::placement_result;std::uintptr_t handle{};
            if(!utils::native_memory::read_bytes(&handle,static_cast<const std::byte*>(entry)+0x68,8))return result::unchanged;
            unsigned part=2;for(unsigned i=0;i<2;++i)if(handle==reinterpret_cast<std::uintptr_t>(&lighting[i]))part=i;
            if(part==2)return result::unchanged;
            snapshot s,now;{const std::lock_guard lock(mutex);s=submitted;now=published;}
            if(!visible(s,part) || !visible(now,part) || s.reference!=now.reference || s.epoch!=now.epoch ||
                s.value.revision!=now.value.revision || s.cap_revision!=now.cap_revision)return result::omit;
            if(part==0 && s.value.stage==phase::dropped)
            {native_followed_fx::position(s.asset->burning,s.ignition,compose(s.root,s.asset->tip));return result::retain;}
            if(part==1 && s.value.cap_removed && !vr::valid_hand(s.cap_hand))return result::retain;
            std::array<float,12> camera{};vec origin{};attachments::solved pose;
            if(!utils::native_memory::read_bytes(camera.data(),static_cast<const std::byte*>(p.record)+engine_stereo_view::h2_view_origin_offset,sizeof(camera)) ||
                !attachments::for_record(p.record,camera,pose) || pose.reference!=s.reference ||
                !utils::native_memory::read_bytes(origin.data(),static_cast<const std::byte*>(p.record)+engine_stereo_view::h2_current_model_placement_origin_offset,sizeof(origin)))return result::omit;
            anchor at;
            if(part==1 && s.value.cap_removed)
            {
                const auto h=unsigned(s.cap_hand);if(h>1)return result::omit;
                at=compose({add(pose.wrists[h].position,origin),pose.wrists[h].rotation},s.cap_grasp);
            }
            else
            {
                if(s.value.stage==phase::stowed)at=stowed(attachments::body_frame(pose,origin),s.asset->center,s.asset->half_height);
                else if(s.value.held())
                {
                    const auto h=unsigned(s.value.holder);
                    at=compose({add(pose.wrists[h].position,origin),pose.wrists[h].rotation},authored::attachment(h,pose.mirror_basis[h]));
                }
                else return result::omit;
                if(part)
                {
                    if(vr::valid_hand(s.value.opener) &&
                        std::chrono::duration<float>(pose.at-s.cap_grasp_at).count()>=.09f)
                    {
                        // Once the hand has snapped on, use this exact skin
                        // record. During acquisition the attached cap stays put.
                        const auto h=unsigned(s.value.opener);
                        at=compose({add(pose.wrists[h].position,origin),pose.wrists[h].rotation},s.cap_grasp);
                    }
                    else at=attached_cap(s,at);
                }
            }
            if(!part && s.value.lit)native_followed_fx::position(s.asset->burning,s.ignition,compose(at,s.asset->tip));
            at=rigid_delta(at,s.asset->bind[part]);
            std::copy(at.position.begin(),at.position.end(),current.origin);std::copy(at.rotation.begin(),at.rotation.end(),current.quat);previous=current;return result::replace;
        }
        void advance_drop(reload_items::flight& flight,anchor& at,bool& rest,clock::time_point now)
        {
            if(rest || !flight.valid())return;
            const auto next=flight.pose(now);game::trace_t hit{};game::Bounds bounds{};
            bounds.halfSize[0]=bounds.halfSize[1]=bounds.halfSize[2]=.5f;
            game::G_TraceCapsule(&hit,at.position.data(),next.position.data(),&bounds,0,0x280e831);
            if(!std::isfinite(hit.fraction) || hit.startsolid || hit.allsolid){rest=true;return;}
            at.position=add(at.position,scale(sub(next.position,at.position),std::clamp(hit.fraction,0.f,1.f)));
            rest=hit.fraction<1 || now-flight.born>5s;
        }
    }
    void initialize()
    {
        native_followed_fx::initialize();weapons::native_weapon_sound::initialize();
        scheduler::loop(feedback,scheduler::pipeline::main);
        scheduler::loop(refresh,scheduler::pipeline::main,250ms);scene_models::on_submit(submit);scene_models::on_prepare_placement(prepare);
        command::add("vr_signalFlare_status",[]{scheduler::once([]{const auto text=status()+mission::status();console::print_text(console::con_type_info,text);
            scheduler::once([text]{utils::io::write_file_atomic("minidumps/h2-mod-vr-signal-flare.txt",text);},scheduler::pipeline::async);},scheduler::pipeline::server);});
    }
    void retire()
    {
        if(asset)native_followed_fx::stop(asset->burning);
        held.reset();intent.reset();trigger_intent.reset();motion.reset();cap_hand=hand::none;++cap_revision;enabled=false;weapon=0;epoch=0;
        {const std::lock_guard lock(mutex);published={};submitted={};}asset.reset();attempted=nullptr;
    }
    bool collect(const hi::frame& f,bool active)noexcept
    {
        enabled=active && mission::active();
        if(!enabled){intent.reset();trigger_intent.reset();return false;}
        if(epoch!=mission::generation() || timeline!=weapons::native_ammunition::timeline())reset();
        reference=f.input.reference_generation;sample_at=f.input.sampled_at;
        if(!weapon)for(unsigned i=1;i<512;++i){const auto* def=game::weapon_defs[i];if(def && def->szInternalName && std::string_view(def->szInternalName)=="flare"){weapon=i;break;}}
        snapshot s;{const std::lock_guard lock(mutex);s=published;}
        publish(&f);
        if(!weapon || !s.asset || !s.hands_ready)return true;
        if(held.held())root=held_root(f.wrists,s);
        const auto pending=abdominal_intent(intent,f);const bool authorized=mission::authorized();
        const auto trigger_pending=abdominal_intent(trigger_intent,f,hi::button::trigger);
        for(unsigned h=0;h<2;++h)
        {
            const auto actor=hand(h);const auto edge=hi::input(actor,hi::button::grip);
            const auto trigger=hi::input(actor,hi::button::trigger);
            if(!hi::free(actor) || !(f.valid_hands&(1u<<h)))continue;
            const bool pickup=held.stage==phase::stowed;
            if(pickup)
            {
                if(!edge.down || !(pending&(1u<<h)))continue;
                const auto slot=stowed_slot(f.body,s.asset->half_height);
                const auto d=abdominal_grab_distance_at(f.body,slot.position,f.wrists[h],s.basis[h],s.mirror[h],h);
                if(d<=1)hi::offer({actor,{target(0,held.revision+1),hi::role::control,hi::button::grip,hi::recipe::single,hi::capability::action},edge.event,20,d,1,true,true});
            }
            else if(((pending|trigger_pending)&(1u<<h)) && held.held() && actor!=held.holder && !vr::valid_hand(held.opener))
            {
                const auto palm=knife_profile::palm_contact(f.wrists[h],s.basis[h],s.mirror[h],h==1);
                const auto distance=[&](vec point){return std::min(length(sub(palm,point)),length(sub(f.wrists[h].position,point)))/(f.body.units_per_meter*.08f);};
                const auto cap_world=compose(root,s.asset->cap);
                const auto tracked=wrist(f.wrists[h],s,h);
                const auto grasp=authored::choose_cap_attachment(h,s.mirror[h],tracked.rotation,cap_world.rotation);
                // Use the actual authored cap grasp as well as wrist/palm
                // proximity, so the hand can snap from a natural approach.
                const auto predicted=compose(tracked,grasp).position;
                const auto cap=std::min(distance(compose(root,{s.asset->cap_center,{0,0,0,1}}).position),
                    length(sub(predicted,cap_world.position))/(f.body.units_per_meter*.08f));
                const auto tube=distance(compose(root,{{0,0,1.f},{0,0,0,1}}).position);
                if(authorized && !held.lit && cap<=1)
                {
                    if(edge.down && (pending&(1u<<h)))
                        hi::offer({actor,{target(1),hi::role::part,hi::button::grip,hi::recipe::single,hi::capability::action},edge.event,10,cap,1,true,true});
                    if(trigger.down && (trigger_pending&(1u<<h)))
                        hi::offer({actor,{target(1),hi::role::part,hi::button::trigger,hi::recipe::single,hi::capability::action},trigger.event,10,cap,1,true,true});
                }
                else if(edge.down && edge.press && tube<=1)
                    hi::offer({actor,{target(2),hi::role::support,hi::button::grip,hi::recipe::single,hi::capability::action},edge.event,15,tube,1,true,true});
            }
        }
        return true;
    }
    void update()noexcept
    {
        const auto* frame=hi::simulation();if(!frame || !enabled || !weapon)return;const auto& f=*frame;
        snapshot s;{const std::lock_guard lock(mutex);s=published;}if(!s.asset)return;
        for(unsigned h=0;h<2;++h)
        {
            const auto actor=hand(h);
            if(held.stage==phase::stowed && hi::granted(actor,hi::domain::special,{weapon,held.revision+1},hi::button::grip,hi::role::control))
            {held.take(actor);motion.reset();++takes;weapons::feedback::carry_confirmation(actor,f.input);}
            else if(held.held() && hi::granted(actor,hi::domain::special,{weapon,held.revision},hi::button::grip,hi::role::support))
            {const auto previous=target(2);if(held.handoff(actor)){hi::completed(actor,previous);motion.reset();++handoffs;}}
            else if(held.held() && actor!=held.holder &&
                (hi::granted(actor,hi::domain::special,{weapon,held.revision},hi::button::grip,hi::role::part) ||
                 hi::granted(actor,hi::domain::special,{weapon,held.revision},hi::button::trigger,hi::role::part)))
            {
                root=held_root(f.wrists,s);
                const auto raw=wrist(f.wrists[h],s,h);
                if(held.start_pull(actor,compose(inverse(root),raw).position,mission::authorized()))
                {
                    cap_button=hi::granted(actor,hi::domain::special,{weapon,held.revision},hi::button::grip,hi::role::part)?hi::button::grip:hi::button::trigger;
                    cap_grasp=authored::choose_cap_attachment(h,s.mirror[h],raw.rotation,compose(root,s.asset->cap).rotation);
                    cap_grasp_at=f.input.sampled_at;++pulls;weapons::feedback::carry_confirmation(actor,f.input);
                }
            }
        }
        if(held.held())
        {
            const auto h=unsigned(held.holder);
            const bool tracked=(f.valid_hands&(1u<<h)) && !weapons::carry::hand_has_weapon(held.holder) && hi::has(held.holder,hi::domain::special);
            if(tracked)root=held_root(f.wrists,s);
            if(!tracked){held.cancel_pull();motion.reset();} // A transient pose/lease interruption is not a drop.
            else
            {
                motion.sample(root.position,f.input.sampled_at,f.body.units_per_meter);
                const auto grip=hi::input(held.holder,hi::button::grip);
                if(grip.release || (grip.armed && f.input.squeeze[h].active && !f.input.squeeze[h].down))
                {
                    body_flight={};body_flight.start=root;body_flight.units=f.body.units_per_meter;body_flight.born=f.input.sampled_at;
                    body_flight.velocity=motion.velocity(f.input.sampled_at,f.body.units_per_meter);body_rest=false;held.release(true);
                }
                else if(vr::valid_hand(held.opener))
                {
                    const auto other=unsigned(held.opener);const auto button=hi::input(held.opener,cap_button);
                    if(button.release || !button.down || !(f.valid_hands&(1u<<other)) || !hi::has(held.opener,hi::domain::special))held.cancel_pull();
                    else if(held.sample_pull(compose(inverse(root),wrist(f.wrists[other],s,other)).position,f.body.units_per_meter,mission::authorized()))
                    {
                        const auto opener=held.opener;
                        if(held.ignite(mission::ignite()))
                        {
                            cap_hand=opener;++cap_revision;cap_visible=true;lit_at=game::CG_GetGameTime(0);ignition=++next_ignition;
                            weapons::feedback::carry_confirmation(held.holder,f.input);weapons::feedback::carry_confirmation(cap_hand,f.input);
                        }
                    }
                }
            }
        }
        if(vr::valid_hand(cap_hand))
        {
            const auto h=unsigned(cap_hand);const auto grip=hi::input(cap_hand,cap_button);
            if(weapons::carry::hand_has_weapon(cap_hand))
            {cap_hand=hand::none;cap_visible=false;++cap_revision;}
            else if(f.valid_hands&(1u<<h))
            {
                cap_root=compose(wrist(f.wrists[h],s,h),cap_grasp);
                const auto& action=cap_button==hi::button::trigger?f.input.trigger[h]:f.input.squeeze[h];
                if(grip.release || (grip.armed && action.active && !action.down))
                {cap_flight={};cap_flight.start=cap_root;cap_flight.units=f.body.units_per_meter;cap_flight.born=f.input.sampled_at;cap_falling=true;cap_hand=hand::none;++cap_revision;}
            }
        }
        if(held.stage==phase::dropped)advance_drop(body_flight,root,body_rest,f.input.sampled_at);
        if(cap_falling){bool rest{};advance_drop(cap_flight,cap_root,rest,f.input.sampled_at);if(rest || !cap_flight.alive(f.input.sampled_at)){cap_falling=cap_visible=false;}}
        const int time=game::CG_GetGameTime(0);
        last_time=time;publish(&f);
    }
    void report()noexcept
    {
        if(held.held())hi::observed(held.holder,{target(),hi::role::control,hi::button::grip,hi::recipe::single,hi::capability::action});
        if(vr::valid_hand(held.opener))hi::observed(held.opener,{target(1),hi::role::part,cap_button,hi::recipe::single,hi::capability::action});
        if(vr::valid_hand(cap_hand))hi::observed(cap_hand,{target(3,cap_revision),hi::role::part,cap_button,hi::recipe::single,hi::capability::action});
    }
    void lifecycle(bool suspended)noexcept
    {
        if(!scheduler::is_executing(scheduler::pipeline::server))return;
        if(epoch!=mission::generation() || timeline!=weapons::native_ammunition::timeline())reset();
        const auto input=controller_input::latest();
        bool dead{};
        const bool player_dead=game::CL_IsCgameInitialized() && player_life::read(game::g_entities[0].client,dead) && dead;
        if(!mission::active() || player_dead)
        {intent.reset();trigger_intent.reset();held.release(false);cap_hand=hand::none;cap_visible=cap_falling=false;++cap_revision;if(asset)native_followed_fx::stop(asset->burning);}
        else if(suspended || reference!=input.reference_generation || !fresh(input.sampled_at))
        {intent.reset();trigger_intent.reset();held.cancel_pull();motion.reset();} // Retain tube/cap until a real release or level reset.
        if(last_time && game::CL_IsCgameInitialized() && game::CG_GetGameTime(0)<last_time)reset();
        publish();
    }
    void present(const hands::interaction_rig& parts,const rig& r,const controller_input::frame& input,const std::array<anchor,2>& targets,
        const std::array<vec,2>& shoulders,const std::array<vec,3>& axes,float units,std::span<bone> solved,unsigned occupied,unsigned visible_hands)noexcept
    {
        if(!parts.valid || r.count<=0 || r.count>256 || solved.size()<std::size_t(r.count))return;
        snapshot s;{const std::lock_guard lock(mutex);published.basis=parts.basis;published.hands_ready=true;
            for(unsigned h=0;h<2;++h)published.mirror[h]=parts.library.mirror_basis[r.arms[h].wrist];s=published;}
        if(!s.enabled || !s.asset || !fresh(s.at) || s.reference!=input.reference_generation)return;
        for(unsigned h=0;h<2;++h)
        {
            if(!(visible_hands&(1u<<h)) || (occupied&(1u<<h)))continue;
            if(s.value.holder!=hand(h) && s.value.opener!=hand(h) && s.cap_hand!=hand(h))continue;
            if(s.value.opener==hand(h) && s.value.held())
            {
                const auto rear=unsigned(s.value.holder);
                const auto base=compose({solved[r.arms[rear].wrist].position,normalize(multiply(targets[rear].rotation,s.basis[rear]))},authored::attachment(rear,s.mirror[rear]));
                auto cap=s.asset->cap;const auto local=compose(inverse(base),wrist(targets[h],s,h));
                cap.position[2]+=std::clamp(local.position[2]-s.value.pull_start[2],0.f,units*.04f);
                auto desired=compose(compose(base,cap),inverse(s.cap_grasp));
                const float t=std::clamp(std::chrono::duration<float>(input.sampled_at-s.cap_grasp_at).count()/.09f,0.f,1.f);
                const float amount=t*t*(3-2*t);const auto before=as_anchor(solved[r.arms[h].wrist]);
                desired.position=add(scale(before.position,1-amount),scale(desired.position,amount));
                desired.rotation=blend_quat(before.rotation,desired.rotation,amount);
                (void)weapons::constrain_part_hand(r,parts.library,hands::native_hand_schema::definition,targets,shoulders,axes,int(rear),desired,solved);
            }
            else move_part(r,r.arms[h].wrist,{solved[r.arms[h].wrist].position,normalize(multiply(targets[h].rotation,parts.basis[h]))},solved);
            const bool cap=s.value.opener==hand(h) || s.cap_hand==hand(h);
            hands::pose_mirror::fingers(r,parts.library,hands::native_hand_schema::definition,cap?authored::cap_fingers:authored::fingers,h,solved,cap?h==1:h==0);
        }
    }
    std::string status()
    {
        const std::lock_guard lock(mutex);
        return std::format("[VR signal flare] enabled={} weapon={} phase={} hand={} opener={} lit={} cap_hand={} takes={} handoffs={} pulls={} fx_failures={} sounds={} missing_sounds={} resources={}\n",
            published.enabled,published.weapon,int(published.value.stage),int(published.value.holder),int(published.value.opener),published.value.lit,int(published.cap_hand),takes.load(),handoffs.load(),pulls.load(),fx_failed.load(),sounds_played.load(),sounds_failed.load(),reason.load());
    }
}
