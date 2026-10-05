#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "component/vr/gameplay/hand_rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "scripted_arms_runtime.hpp"
#include "scripted_arms_pose.hpp"
#include "scripted_sequences.hpp"
#include "native_animation_query.hpp"
#include "native_hand_rig.hpp"
#include "cliffhanger_physical.hpp"
#include "hand_position_offset.hpp"
#include "hand_pose_library.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::scripted_arms
{
    namespace
    {
        using namespace hands;
        using clock=controller_input::clock;
        std::mutex mutex;
        std::atomic_bool alive{true};
        const char* reason="no scripted body requested";
        std::uint64_t applications{},evaluations{},rejections{},tag_only_queries{};
        std::array<float,2> weights{};
        unsigned tracked{};
        bool lookup_ready()
        {
            static const bool valid=[] {
                constexpr std::uint8_t bytes[]{0x48,0x63,0xc1,0x48,0x8d,0x0d,0xa6,0xc2,0xb6,0x0a,0x0f,0xbf,0x0c,0x41,0x85,0xc9};
                std::array<std::uint8_t,sizeof(bytes)> mask{};mask.fill(255);
                return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(vr::h2::sp::client_entity_dobj.address()),{bytes,mask.data(),sizeof(bytes)}));
            }();
            return valid;
        }
        sequences::view current()
        {
            if(!alive || !head_pose_bridge::get_status().enabled || !game::CL_IsCgameInitialized())return {};
            const auto* enabled=game::Dvar_FindVar("vr_independentHands");
            if(!enabled || !enabled->current.enabled)return {};
            const auto state=sequences::for_player(game::CG_GetPredictedPlayerState(0));
            // The legacy Estate arm-hiding path remains a distinct presentation.
            return state.epoch && state.position_epoch && !state.hide_body_arms && state.arms.reserved() ? state : sequences::view{};
        }
        bool matches(const void* object,const sequences::view& state)
        {
            return object && state.arms.reserved() && lookup_ready() &&
                object==vr::h2::sp::client_entity_dobj(state.arms.entity,0);
        }
        struct binding_key
        {
            const void* object{};
            std::uint64_t scene{},source{};
            int entity{-1};model_profile profile{};
            unsigned models{},bones{},duplicates{};
            std::array<game::XModel*,32> model{};
            std::array<const game::DObjAnimMat*,32> bind{};
            bool operator==(const binding_key&) const = default;
        };
        struct binding
        {
            binding_key key{};rig layout{};
            int body_model{-1},torso{-1};
            std::array<quat,2> basis{};
            forearm_twist_binding twist{};
            bool valid{};
        } bound;
        transition motion;
        forearm_twist twist;
        clock::time_point sampled_at{};
        // Completion is deliberately independent of tracking/reference/clip changes:
        // a second eye cannot retry a rejected pose or solve an already solved pose.
        once_per_pose completed;
        const weapons::profile* grasp_profile{};pose_library grasp_library{};
        bool bind(const native_object& object,const sequences::view& state,const profile& profile)
        {
            if(!object.models || !object.model_count || object.model_count>32 || !object.bone_count)
            {reason="native body metadata unavailable";return false;}
            binding_key key{&object,state.epoch,state.position_epoch,state.arms.entity,state.arms.profile,
                object.model_count,object.bone_count,object.duplicate_parts};
            for(unsigned m=0;m<object.model_count;++m)
            {
                key.model[m]=object.models[m];if(!key.model[m])return false;
                key.bind[m]=key.model[m]->baseMat;
            }
            if(key==bound.key)return bound.valid;
            // Appending/removing a wrist prop rebuilds topology but does not
            // restart the stand-up or let go of already controlled arms.
            const bool same_body=bound.valid && bound.key.object==key.object && bound.key.scene==key.scene &&
                bound.key.source==key.source && bound.key.entity==key.entity && bound.key.profile==key.profile &&
                key.model[bound.body_model]==bound.key.model[bound.body_model] && key.bind[bound.body_model]==bound.key.bind[bound.body_model];
            if(!same_body){motion.reset();twist.reset();sampled_at={};}
            // Native attach/detach preserves the body transition but changes
            // topology; rebuild preset locals for the complete new skeleton.
            grasp_profile=nullptr;
            bound={};bound.key=key;
            std::array<model_definition,32> models{};std::array<bone_definition,256> bones{};
            if(!describe(object,models,bones)){reason="native body hierarchy rejected";return false;}
            const auto resolved=resolve_rig({models.data(),object.model_count},{bones.data(),object.bone_count},rig_kind::scripted_body);
            if(resolved.rejection){reason=resolved.rejection;return false;}
            const auto body=models[resolved.hands_model];
            if(body.name!=profile.model || !key.bind[resolved.hands_model]){reason="unreviewed scripted body model";return false;}
            int torso=-1;
            for(int i=body.begin;i<body.begin+body.count;++i)
            {
                if(!finite_twist_pose(bones[i].bind)){reason="invalid native body bind pose";return false;}
                if(bones[i].name==profile.torso)
                {if(torso>=0){reason="ambiguous native torso";return false;}torso=i;}
                if((bones[i].name=="tag_origin" || bones[i].name=="tag_player" || bones[i].name=="tag_camera" ||
                    bones[i].name=="tag_view" || bones[i].name=="tag_torso") && arm_owner(resolved.layout,i)>=0)
                {reason="camera/body anchor belongs to an arm";return false;}
            }
            if(torso<0 || arm_owner(resolved.layout,torso)>=0){reason="native torso anchor unavailable";return false;}
            bound.layout=resolved.layout;bound.body_model=resolved.hands_model;bound.torso=torso;
            const auto inverse=conjugate(normalize(bones[bound.layout.weapon_tag].bind.rotation));
            for(unsigned h=0;h<2;++h)bound.basis[h]=normalize(multiply(inverse,normalize(bones[bound.layout.arms[h].wrist].bind.rotation)));
            bound.twist=bind_forearm_twist(bound.layout,{bones.data(),object.bone_count},body);
            bound.valid=true;return true;
        }
        bool fresh(clock::time_point at)
        {const auto now=clock::now();return at!=clock::time_point{} && now>=at && now-at<=150ms;}
        float setting(const char* name,float fallback)
        {const auto* d=game::Dvar_FindVar(name);return d ? d->current.value : fallback;}
    }
    bool owns(const void* object) noexcept {return matches(object,current());}
    bool wants_pose(const void* pointer,const unsigned* requested) noexcept
    {
        const auto state=current();if(!requested || !matches(pointer,state) || state.arms.mode!=control::tracked ||
            cliffhanger_physical::preserve_native_arms(state.arms.entity))return false;
        const auto* profile=find_profile(state.arms.profile);if(!profile)return false;
        const std::lock_guard lock(mutex);
        if(!bind(*static_cast<const native_object*>(pointer),state,*profile))return false;
        // Camera/tag queries must not pre-complete the body skeleton before
        // its native pose controllers run. Wait until an arm is actually needed.
        if(!requests_arm_pose(bound.layout,{requested,8})){++tag_only_queries;return false;}
        return true;
    }
    void apply(void* pointer,bool rebuilt) noexcept
    {
        const auto state=current();if(!matches(pointer,state))return;
        auto& object=*static_cast<native_object*>(pointer);
        // The native lock is already held. This mutex protects only this solver
        // and diagnostic counters; no cross-object/tag calls are made under it.
        const std::lock_guard lock(mutex);
        if(!completed.begin({reinterpret_cast<std::uintptr_t>(pointer),reinterpret_cast<std::uintptr_t>(object.matrices),object.timestamp},rebuilt))return;
        ++evaluations;
        weights={};tracked=0;
        const auto reject=[&](const char* text){reason=text;++rejections;motion.reset();twist.reset();sampled_at={};};
        const auto* profile=find_profile(state.arms.profile);
        if(!profile || !bind(object,state,*profile)){reject(profile ? reason : "scripted arm profile missing");return;}
        for(unsigned b=0;b<object.bone_count;++b)if(!(object.calculated[b/32]&(0x80000000u>>(b%32))))
        {reject("incomplete native body pose");return;}
        const auto& layout=bound.layout;const std::span<const bone> native{object.matrices,size_t(layout.count)};
        if(state.arms.mode==control::authored || cliffhanger_physical::preserve_native_arms(state.arms.entity))
        {reject("authored resting/entry/exit arms");return;}
        const auto animation=native_animation::sample(object.tree);
        if(!animation.valid || !animation.time_valid){reject("native animation progress unavailable");return;}
        const float authored=animation_weight(*profile,animation.leaves());
        if(authored==0){reject("authored resting/start pose");return;}
        const auto input=controller_input::latest();head_pose_bridge::spatial_frame spatial;
        const auto* paused=game::Dvar_FindVar("cl_paused");
        const bool tracking=paused && !paused->current.integer && !*game::keyCatchers && input.focused && fresh(input.sampled_at) &&
            input.reference_generation && head_pose_bridge::get_spatial_frame(spatial) && fresh(spatial.captured_at) &&
            spatial.generation==input.reference_generation;
        if(!tracking){reject("body tracking paused or unavailable");return;}
        if(sampled_at!=clock::time_point{} && (input.sampled_at<sampled_at || input.sampled_at-sampled_at>150ms))motion.reset();
        sampled_at=input.sampled_at;
        const auto* view=*reinterpret_cast<const std::byte* const*>(0x141E39D30);
        if(!view){reject("render origin unavailable");return;}
        vec offset{};std::memcpy(offset.data(),view+0x58,sizeof(offset));
        position_offsets offsets;
        offsets={setting("vr_handOffsetInward",offsets.inward_meters),setting("vr_handOffsetBack",offsets.back_meters),setting("vr_handOffsetUp",offsets.up_meters)};
        std::array<anchor,2> targets{};std::array<vec,2> shoulders{};
        for(unsigned h=0;h<2;++h)
        {
            const auto& wrist=native[layout.arms[h].wrist];targets[h]={wrist.position,normalize(wrist.rotation)};
            shoulders[h]=native[layout.arms[h].shoulder].position;
            anchor target;
            if(tracking && (state.arms.hands&(1u<<h)) && tracked_wrist(input,spatial,offset,h,offsets,target))
            {targets[h]={target.position,normalize(multiply(target.rotation,bound.basis[h]))};tracked|=1u<<h;}
            if((tracked&(1u<<h)) && (state.arms.locked&(1u<<h)) && state.arms.reference==input.reference_generation)
                targets[h]={sub(state.arms.grips[h].position,offset),state.arms.grips[h].rotation};
        }
        weights=motion.update(input.reference_generation,state.command_time,tracked,authored);
        if(weights[0]==0 && weights[1]==0){reason="waiting for tracked arm transition";return;}
        // Elbows use the authored torso frame, never the independently turning HMD.
        const auto q=normalize(native[bound.torso].rotation);
        const std::array<vec,3> axis{rotate(q,{1,0,0}),rotate(q,{0,1,0}),rotate(q,{0,0,1})};
        std::array<bone,256> solved{},blended{};std::array<bool,2> limited{};
        std::array<vec,2> elbow_targets{};std::array<const vec*,2> elbow_hints{};
        for(unsigned h=0;h<2;++h)if(state.arms.outward_elbows&(1u<<h))
        {
            const auto outward=unit(sub(shoulders[h],shoulders[1-h]));
            const float upper=length(sub(native[layout.arms[h].elbow].position,shoulders[h]));
            elbow_targets[h]=add(shoulders[h],scale(sub(outward,scale(axis[2],.35f)),upper));
            elbow_hints[h]=&elbow_targets[h];
        }
        // The shoulders belong to a skinned full body. Keep native limb lengths;
        // viewmodel reach stretching pulls the arm/chest seam apart on this rig.
        if(!solve_arms(layout,native,targets,shoulders,axis,{solved.data(),size_t(layout.count)},limited,1.f,elbow_hints))
        {reject("scripted arm IK rejected");return;}
        if(state.arms.hand_pose && state.arms.grasp_hands && state.arms.reference==input.reference_generation)
        {
            if(grasp_profile!=state.arms.hand_pose)
            {
                std::array<model_definition,32> models{};std::array<bone_definition,256> bones{};
                if(!describe(object,models,bones)){reject("grasp hierarchy unavailable");return;}
                grasp_library=bind_weapon_poses(layout,{bones.data(),object.bone_count},*state.arms.hand_pose);
                grasp_profile=state.arms.hand_pose;
            }
            const std::array<float,2> amounts{float(bool(state.arms.grasp_hands&1)),float(bool(state.arms.grasp_hands&2))};
            if(!apply_poses(layout,grasp_library,*state.arms.hand_pose,targets,amounts,false,{solved.data(),size_t(layout.count)}))
            {reject("scripted grasp pose unavailable");return;}
        }
        twist.update(bound.twist,layout,{solved.data(),size_t(layout.count)},input.reference_generation,input.sampled_at,tracked);
        if(!blend_pose(layout,native,{solved.data(),size_t(layout.count)},weights,{blended.data(),size_t(layout.count)}))
        {reject("scripted arm blend rejected");return;}
        if(state.arms.attached_model && state.arms.attached_bone && state.arms.attached_hand<2)
        {
            unsigned base{};
            for(unsigned m=0;m<object.model_count;++m)
            {
                const auto* model=object.models[m];
                if(m!=unsigned(bound.body_model) && model->name && std::string_view(model->name)==state.arms.attached_model)
                    for(unsigned b=0;b<model->numBones;++b)
                    {
                        const auto* name=game::SL_ConvertToString(model->boneNames[b]);
                        if(!name || std::string_view(name)!=state.arms.attached_bone)continue;
                        if(!pose_attached_prop(layout,int(base+b),state.arms.attached_hand,state.arms.attached_in_wrist,
                            {blended.data(),size_t(layout.count)})){reject("held prop is not a valid wrist descendant");return;}
                    }
                base+=model->numBones;
            }
        }
        // Full-body animation remains native. Only admitted arm descendants,
        // including original wrist props, are committed to this locked DObj.
        for(int b=0;b<layout.count;++b)
        {const int h=arm_owner(layout,b);if(h>=0 && weights[h]>0)object.matrices[b]=blended[b];}
        ++applications;reason="native body arms controlled";
    }
    std::string status()
    {
        const std::lock_guard lock(mutex);
        return std::format("scripted_arms={} entity={} profile={} valid={} evaluations={} applications={} rejections={} tag_only_queries={} stretch_limit=1 tracked={} blend_left={} blend_right={}\n",
            reason,bound.key.entity,int(bound.key.profile),bound.valid,evaluations,applications,rejections,tag_only_queries,tracked,weights[0],weights[1]);
    }
    class component final:public component_interface
    {
        void post_unpack() override
        {
            command::add("vr_scripted_arms_status",[] {
                const auto report=status();console::info("%s",report.c_str());
                utils::io::write_file_atomic("minidumps/h2-mod-vr-scripted-arms.txt",report);
            });
        }
        void pre_destroy() override {alive=false;}
    };
}
REGISTER_COMPONENT(vr::gameplay::scripted_arms::component)
