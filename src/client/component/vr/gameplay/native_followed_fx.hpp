#pragma once
#include "hands/pose_solver.hpp"
#include <cstdint>
#include <span>

namespace game {struct FxEffectDef;}
namespace vr::gameplay::native_followed_fx
{
    struct handle {unsigned slot{8};std::uint64_t generation{};explicit operator bool()const noexcept{return slot<8 && generation;}};
    bool initialize() noexcept;
    game::FxEffectDef* restore_definition(game::FxEffectDef*,unsigned attached_mask);
    // Main asset owner only. Copies immutable definition metadata; the native
    // zone keeps ownership of materials/graphs. Retired at the drained DB boundary.
    handle create(game::FxEffectDef*,std::span<const unsigned> attached_elements);
    // Main/client owner queues ONE native effect for this activation.
    bool start(handle,std::uint64_t activation,int game_time,hands::anchor);
    // Render providers can publish exact solved world poses without engine calls.
    void position(handle,std::uint64_t activation,hands::anchor) noexcept;
    void stop(handle) noexcept;
}
