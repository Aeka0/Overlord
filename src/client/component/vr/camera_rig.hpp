#pragma once
#include "scripted_camera.hpp"
#include "scripted_position.hpp"
#include "scripted_view.hpp"
#include "pose_filter.hpp"
#include "continuous_view_angles.hpp"

namespace vr::game_view
{
    using camera_axis=std::array<std::array<float,3>,3>;
    struct camera_input
    {
        camera_axis native_axis{},head_axis{};
        std::array<float,3> head_meters{};
        float native_heading{},head_heading{},command_head_heading{};
        int time{};
        std::uint64_t reference{};
    };
    struct camera_output
    {
        camera_axis axis{},base_axis{};
        std::array<float,3> head_offset{};
        float base_heading{};
        float world_heading{};
        bool spatial{};
    };

    // Engine-neutral composition and lifecycle. The engine adapter supplies
    // one unmodified native pose, one tracked pose and one source lifetime.
    class camera_rig
    {
        scripted_view heading_;
        scripted_position position_;
        std::uint64_t limited_epoch_{},limited_reference_{};
        camera_axis limited_baseline_{pose_filter::identity},limited_delta_{pose_filter::identity};
        std::uint64_t full_epoch_{},full_source_{};
        int full_time_{};
        camera_axis full_base_{pose_filter::identity},full_previous_{pose_filter::identity};
        bool full_valid_{};
        camera_axis last_axis_{pose_filter::identity};
        std::uint64_t last_epoch_{},last_reference_{};
        int last_time_{};
        head_rotation last_head_mode_{head_rotation::free};
        continuous_angles limited_angles_;
        continuous_angles source_angles_;
        script_axes source_axes_{script_axes::yaw};
        std::array<float,3> limited_unwrapped_{};
        horizontal_heading world_heading_;
        std::uint64_t aligned_entry_{};

        bool apply_entry_alignment(const camera_input& input,const camera_request& request,
            std::uint64_t epoch,std::uint64_t entry,bool source_valid,float source_heading,float head_heading) noexcept
        {
            // One shared cut rule: arm when alignment is requested, wait for
            // a valid new anchor, consume once per native entry lifetime.
            // Returning to preserve mode rearms a later phase on the same rig.
            if(request.policy.entry!=camera_entry::align || !entry){aligned_entry_=0;return false;}
            if(entry==aligned_entry_ || !source_valid)return false;
            full_epoch_=0;full_valid_=false;
            // Script-owned shots keep the basis; player-owned cuts pass it
            // through the normal command/record bridge and immediately resume
            // native input. Neither path reseeds on subsequent head motion.
            heading_.align_entry(epoch?epoch:entry,input.reference,input.time,source_heading,head_heading);
            aligned_entry_=entry;
            return true;
        }
        static camera_axis yaw_axis(float yaw) noexcept
        {
            const float r=yaw*pose_filter::radians,c=std::cos(r),s=std::sin(r);
            return {{{c,s,0},{-s,c,0},{0,0,1}}};
        }
        static float yaw(const camera_axis& axis) noexcept
        {
            const auto& f=axis[0];const auto& l=axis[1];
            return f[0]*f[0]+f[1]*f[1]>1e-6f ? std::atan2(f[1],f[0])*57.29577951308232f :
                std::atan2(-l[0],l[1])*57.29577951308232f;
        }
        static camera_axis angles_axis(const std::array<float,3>& degrees) noexcept
        {
            const float p=degrees[0]*pose_filter::radians,y=degrees[1]*pose_filter::radians,r=degrees[2]*pose_filter::radians;
            const float cp=std::cos(p),sp=std::sin(p),cy=std::cos(y),sy=std::sin(y),cr=std::cos(r),sr=std::sin(r);
            return {{{cp*cy,cp*sy,-sp},{-sy*cr+sp*cy*sr,cy*cr+sp*sy*sr,cp*sr},
                {sy*sr+sp*cy*cr,-cy*sr+sp*sy*cr,cp*cr}}};
        }
        camera_axis without_pitch(const camera_axis& source,bool continuous) noexcept
        {
            if(!continuous)
            {
                constexpr float degrees=57.29577951308232f;
                source_angles_={std::atan2(source[0][2],std::hypot(source[0][0],source[0][1]))*degrees,
                    yaw(source),std::atan2(source[1][2],source[2][2])*degrees};
            }
            else (void)source_angles_.update(source);
            // Remove the native component before relative matrix composition.
            // Continuous branches prevent pitch crossing a pole from becoming
            // a spurious 180-degree yaw/roll change. Never filter the HMD pose.
            return angles_axis({0,source_angles_.yaw,source_angles_.roll});
        }
        camera_axis tracked(const camera_input& input,const camera_request& request) noexcept
        {
            const auto& policy=request.policy;
            if(policy.head==head_rotation::fixed)return pose_filter::identity;
            if(policy.head==head_rotation::pitch_roll)
                return pose_filter::multiply(input.head_axis,pose_filter::transpose(yaw_axis(input.head_heading)));
            if(policy.head!=head_rotation::limited){limited_epoch_=0;return input.head_axis;}
            const auto epoch=request.entry_epoch?request.entry_epoch:request.epoch;
            if(!limited_epoch_ || epoch!=limited_epoch_)
            {limited_baseline_=input.head_axis;limited_delta_=pose_filter::identity;limited_angles_={};limited_unwrapped_={};}
            else if(limited_reference_!=input.reference)
                limited_baseline_=pose_filter::multiply(pose_filter::transpose(limited_delta_),input.head_axis);
            limited_epoch_=epoch;limited_reference_=input.reference;
            limited_delta_=pose_filter::multiply(input.head_axis,pose_filter::transpose(limited_baseline_));
            (void)limited_angles_.update(limited_delta_);
            const std::array<float,3> canonical{{-limited_angles_.pitch,limited_angles_.yaw,limited_angles_.roll}};
            for(unsigned i=0;i<3;++i)limited_unwrapped_[i]+=std::remainder(canonical[i]-limited_unwrapped_[i],360.f);
            auto angles=limited_unwrapped_;
            for(unsigned i=0;i<3;++i)
            {
                const float lo=policy.limits.minimum[i],hi=policy.limits.maximum[i];
                angles[i]=std::isfinite(lo) && std::isfinite(hi) && lo<=hi ?
                    std::clamp(angles[i],std::clamp(lo,-180.f,180.f),std::clamp(hi,-180.f,180.f)):0.f;
            }
            return pose_filter::multiply(angles_axis(angles),limited_baseline_);
        }
    public:
        bool owns_camera() const noexcept {return heading_.owns_camera();}
        bool restore_command(std::uint64_t epoch,std::uint64_t reference,float& yaw,float delta,float head,float native_head,
            const camera_axis& physical_head) noexcept
        {
            if(!epoch && last_epoch_ && last_head_mode_!=head_rotation::free)
            {
                heading_.follow_native(last_epoch_,reference,last_time_,camera_rig::yaw(last_axis_)-camera_rig::yaw(physical_head)+head,head);
                last_epoch_=0;
            }
            return heading_.restore_command(epoch,reference,yaw,delta,head,native_head);
        }
        void record(int time) noexcept {heading_.record(time);}

        camera_output compose(const camera_input& input,const camera_request& request,
            const scripted_rotation_reference* rotation=nullptr) noexcept
        {
            if((last_epoch_ || aligned_entry_ || heading_.owns_camera()) && input.time<last_time_)*this={};
            const auto& policy=request.policy;
            const auto epoch=policy.owns_rotation()?request.epoch:0;
            const bool tag_valid=rotation && rotation->source && pose_filter::valid({{},rotation->axis});
            const bool source_valid=policy.source==rotation_source::view || tag_valid;
            auto source=policy.source==rotation_source::tag && tag_valid ? rotation->axis:input.native_axis;
            const auto source_id=policy.source==rotation_source::tag ? (tag_valid?rotation->source:0):epoch;
            if(source_valid && policy.axes==script_axes::yaw_roll)
                source=without_pitch(source,full_valid_ && full_epoch_==epoch && full_source_==source_id &&
                    source_axes_==policy.axes && input.time>=full_time_);
            source_axes_=policy.axes;
            const bool matrix_axes=policy.axes!=script_axes::yaw;
            const float source_heading=policy.source==rotation_source::tag || policy.axes==script_axes::yaw_roll ? yaw(source):input.native_heading;
            const auto entry=request.entry_epoch?request.entry_epoch:request.position_epoch?request.position_epoch:epoch;
            const auto head=tracked(input,request);
            const float head_heading=policy.head==head_rotation::free?input.head_heading:yaw(head);
            const bool aligned=apply_entry_alignment(input,request,epoch,entry,source_valid,source_heading,head_heading);

            camera_output out;
            if(aligned && matrix_axes)position_={};
            out.head_offset=position_.offset(request.position_epoch,input.reference,input.head_meters,policy);
            float base=std::remainder(input.native_heading-input.command_head_heading,360.f);
            if(epoch && policy.script==script_rotation::override_view && matrix_axes)
            {
                if(source_valid){full_base_=source;full_valid_=true;}
                else if(full_epoch_!=epoch)full_base_=yaw_axis(heading_.compose(epoch,input.reference,input.time,base,head_heading));
                out.base_axis=full_base_;
                base=yaw(out.base_axis);
            }
            else if(epoch && policy.script==script_rotation::additive && matrix_axes)
            {
                base=heading_.compose(epoch,input.reference,input.time,base,head_heading);
                // Full cuts match every axis. Yaw/roll cuts keep physical head
                // tilt on entry too; removing native pitch must not reset it.
                if(aligned)full_base_=pose_filter::multiply(policy.axes==script_axes::yaw_roll ?
                    yaw_axis(-head_heading):pose_filter::transpose(head),source);
                else if(full_epoch_!=epoch)full_base_=yaw_axis(base);
                else
                {
                    if(last_reference_!=input.reference)
                        full_base_=pose_filter::multiply(pose_filter::transpose(head),last_axis_);
                    if(source_valid && full_valid_ && full_source_==source_id && input.time>=full_time_)
                        full_base_=pose_filter::multiply(full_base_,pose_filter::multiply(pose_filter::transpose(full_previous_),source));
                }
                out.base_axis=full_base_;base=yaw(out.base_axis);
            }
            else
            {
                if(epoch && policy.script==script_rotation::additive)
                    base=policy.source==rotation_source::tag ?
                        heading_.follow_authored(epoch,input.reference,input.time,base,head_heading,tag_valid?source_id:0,source):
                        heading_.follow_authored(epoch,input.reference,input.time,base,head_heading,source_id,source_heading);
                else if(epoch && policy.script==script_rotation::override_view && source_valid)
                    base=heading_.follow_native_yaw(epoch,input.reference,input.time,source_heading,head_heading);
                else base=heading_.compose(epoch,input.reference,input.time,base,head_heading);
                out.base_axis=yaw_axis(base);
            }
            if(epoch && policy.script!=script_rotation::ignore && matrix_axes)
            {
                full_epoch_=epoch;full_source_=source_id;full_time_=input.time;full_previous_=source;full_valid_=source_valid;
                const auto world=pose_filter::multiply(head,out.base_axis);
                // Handback uses the actual rendered heading, including partial
                // head constraints, rather than the raw Euler input branch.
                heading_.follow_native(epoch,input.reference,input.time,yaw(world),head_heading);
            }
            else {full_epoch_=0;full_valid_=false;}
            out.axis=pose_filter::multiply(head,out.base_axis);
            out.base_heading=base;
            (void)world_heading_.update(out.axis);out.world_heading=world_heading_.yaw;
            out.spatial=policy.head!=head_rotation::fixed;
            last_axis_=out.axis;last_epoch_=epoch;
            last_reference_=input.reference;last_time_=input.time;last_head_mode_=policy.head;
            return out;
        }
    };
}
