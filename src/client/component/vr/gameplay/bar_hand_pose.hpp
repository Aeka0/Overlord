#pragma once
#include "empty_hand_pose.hpp"

namespace vr::gameplay::hands::bar_grip
{
    // Partially wrapped fingers leave space for a real bar rather than closing
    // the common knife/fist pose onto itself. Left/right anatomy remains native.
    inline constexpr empty_hand::curls wrap{.70f,.60f,.65f};
    struct binding
    {
        quat frame_in_wrist{0,0,0,1};vec centre_in_wrist{};bool valid{}; // source-model units
    };
    inline vec centre(const binding& source,const anchor& wrist,quat basis)noexcept
    {
        return add(wrist.position,rotate(normalize(multiply(wrist.rotation,basis)),source.centre_in_wrist));
    }
    template<class Pose>
    std::array<binding,2> bind(const rig& r,const pose_library& library,std::span<const bone_definition> bones,const Pose& pose)noexcept
    {
        std::array<binding,2> out{};
        if(!library.valid || r.count<=0 || r.count>256 || bones.size()!=size_t(r.count))return out;
        for(unsigned h=0;h<2;++h)
        {
            const auto find=[&](std::string_view left,std::string_view right){
                for(int i=0;i<r.count;++i)if(bones[i].name==(h?right:left))return i;return -1;
            };
            const int index=find("j_index_le_0","j_index_ri_0"),pinky=find("j_pinky_le_0","j_pinky_ri_0"),
                middle=find("j_mid_le_0","j_mid_ri_0"),tip=find("j_mid_le_2","j_mid_ri_2"),wrist=r.arms[h].wrist;
            if(index<0 || pinky<0 || middle<0 || tip<0 || wrist<0 || wrist>=r.count)continue;
            std::array<bone,256> source{};for(int i=0;i<r.count;++i)source[i]=bones[i].bind;
            if(!empty_hand::apply(r,library,pose,vr::hand(h),{true,empty_hand::gesture::fist,wrap},{source.data(),size_t(r.count)}))continue;
            const auto across=sub(source[index].position,source[pinky].position);
            auto forward=sub(source[middle].position,source[wrist].position);
            if(length(across)<.01f || length(forward)<.01f)continue;
            const auto side=unit(across);forward=sub(forward,scale(side,dot(forward,side)));
            if(length(forward)<.01f)continue;
            forward=unit(forward);const auto normal=unit(cross(forward,side));
            const auto q=conjugate(normalize(source[wrist].rotation));
            out[h].frame_in_wrist=normalize(multiply(q,from_axis({forward,side,normal})));
            const auto centre=scale(add(source[middle].position,source[tip].position),.5f);
            out[h].centre_in_wrist=rotate(q,sub(centre,source[wrist].position));out[h].valid=true;
        }
        return out;
    }
    inline anchor on_bar(const binding& source,vec centre,vec outward,unsigned h)noexcept
    {
        const auto forward=unit(scale(outward,-1.f));
        const auto across=scale(unit(cross({0,0,1},forward)),h?1.f:-1.f);
        const auto normal=unit(cross(forward,across));
        const auto rotation=normalize(multiply(from_axis({forward,across,normal}),conjugate(source.frame_in_wrist)));
        // The grasp centre is reconstructed from posed native finger joints.
        // Position the wrist around it; no guessed world-space wrist offset.
        return {sub(centre,rotate(rotation,source.centre_in_wrist)),rotation};
    }
    // Apply world-contact wrists after weapon posing. Only the selected arm
    // branches are committed; the opposite arm, receiver and muzzle stay exact.
    inline bool constrain(const rig& r,std::span<bone> solved,const std::array<anchor,2>& contacts,
        unsigned held,const std::array<vec,2>& shoulders,const std::array<vec,3>& axes)noexcept
    {
        if(!(held&3) || r.count<=0 || r.count>256 || solved.size()<size_t(r.count))return false;
        auto targets=contacts;
        for(unsigned h=0;h<2;++h)
        {
            const auto wrist=r.arms[h].wrist;if(wrist<0 || wrist>=r.count)return false;
            if(!(held&(1u<<h)))targets[h]={solved[wrist].position,solved[wrist].rotation};
        }
        std::array<bone,256> result{};std::array<bool,2> limited{};
        if(!solve_arms(r,solved,targets,shoulders,axes,result,limited))return false;
        for(unsigned h=0;h<2;++h)if(held&(1u<<h))
        {
            const auto arm=r.arms[h];const auto correction=sub(targets[h].position,result[arm.wrist].position);
            for(int i=0;i<r.count;++i)if(!r.weapon_bones[i] && descendant(i,arm.shoulder,r))
            {
                if(descendant(i,arm.wrist,r))result[i].position=add(result[i].position,correction);
                solved[i]=result[i];
            }
        }
        return true;
    }
}
