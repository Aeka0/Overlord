#pragma once
#include "free_climb.hpp"
#include "../controller_input.hpp"
#include <string>

namespace vr::gameplay::cliffhanger_physical
{
    enum class phase { inactive, authored_entry, climbing, authored_exit };
    struct presentation
    {
        phase stage{};int body{-1},carrier{-1};
        std::uint64_t reference{};float units{};unsigned latched{};
        std::array<hands::anchor,2> wrists{}; // fixed world poses, including withdrawal
        std::array<free_climb::contact,2> walls{}; // native world units
        bool active() const noexcept {return stage!=phase::inactive;}
        bool authored() const noexcept {return stage==phase::authored_entry || stage==phase::authored_exit;}
    };
    presentation latest() noexcept;
    bool owns_movement() noexcept;
    bool independent_hands() noexcept;
    bool preserve_native_arms(int body) noexcept;
    void constrain_hands(std::array<hands::anchor,2>&,const std::array<hands::quat,2>& basis,
        hands::vec view_offset,std::uint64_t reference) noexcept;
    void publish_hands(const controller_input::frame&,const std::array<hands::anchor,2>&,hands::vec view_offset) noexcept;
    std::string status();
}
