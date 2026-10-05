#pragma once
#include "cliffhanger_policy.hpp"
#include "equipment_runtime.hpp"
#include "hand_interaction/frame.hpp"

namespace vr::gameplay::equipment::special::cliffhanger
{
    struct pick_geometry
    {
        bool assets{},hands_ready{};
        std::array<hands::quat,2> basis{};
        std::array<hands::anchor,2> attachment{};
        std::array<hands::vec,2> tip{}; // prop root coordinates, native units
    };
    pick_geometry geometry() noexcept;
    struct pickaxe_strike
    {
        hands::anchor root{};
        unsigned weapon{},side{};
        std::uint64_t lease{};
    };
    bool melee_pickaxe(vr::hand,const hands::anchor& wrist,std::uint64_t reference,pickaxe_strike&) noexcept;
    bool current_pickaxe(vr::hand,unsigned side,std::uint64_t lease,std::uint64_t reference) noexcept;
    context latest() noexcept;
    bool active() noexcept;
    bool native_c4_session() noexcept;
    bool preserve_native_selection(unsigned requested) noexcept;
    bool collect(const hand_interaction::frame&,bool enabled) noexcept;
    void update() noexcept;
    void report() noexcept;
    void lifecycle(bool suspended) noexcept;
    void initialize();
    void retire();
    void command(const controller_input::frame&,bool gameplay,int& buttons) noexcept;
    void present(const hands::interaction_rig&,const hands::rig&,const controller_input::frame&,
        const std::array<hands::anchor,2>&,const std::array<hands::vec,2>&,const std::array<hands::vec,3>&,
        float,std::span<hands::bone>,unsigned occupied,unsigned visible) noexcept;
    std::string status();
}
