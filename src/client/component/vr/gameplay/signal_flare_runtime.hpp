#pragma once
#include "special_equipment_runtime.hpp"
namespace vr::gameplay::equipment::special::flare
{
    void initialize();
    void retire();
    bool collect(const hand_interaction::frame&,bool enabled) noexcept;
    void update() noexcept;
    void report() noexcept;
    void lifecycle(bool suspended) noexcept;
    void present(const hands::interaction_rig&,const hands::rig&,const controller_input::frame&,
        const std::array<hands::anchor,2>&,const std::array<hands::vec,2>&,const std::array<hands::vec,3>&,
        float,std::span<hands::bone>,unsigned,unsigned) noexcept;
    std::string status();
}
