#pragma once
#include "hand_interaction/frame.hpp"
#include "hand_service.hpp"
#include <string>
namespace vr::gameplay::ladders
{
    void collect_interactions(const hand_interaction::frame&)noexcept;
    void update_interactions()noexcept;
    void report_interactions()noexcept;
    void lifecycle(bool suspended)noexcept;
    bool owns_movement()noexcept;
    bool owns_carrier(int entity)noexcept;
    bool apply_camera_origin(float* origin,float native_view_height,int linked_entity,int frame_time)noexcept;
    void present(const hands::interaction_rig&,const hands::rig&,const controller_input::frame&,
        const std::array<hands::vec,2>& shoulders,const std::array<hands::vec,3>& axes,hands::vec offset,
        std::span<hands::bone>,unsigned visible)noexcept;
    std::string status();
}
