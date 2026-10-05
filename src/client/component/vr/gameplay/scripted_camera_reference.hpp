#pragma once
#include "campaign/scripted_sequences.hpp"

namespace vr::gameplay::sequences::camera_reference
{
    // Native camera boundary only, before HMD composition. No VM calls or
    // server-rate angle publication: this reads the current client tag pose.
    game_view::scripted_rotation_reference sample(const view&) noexcept;
    std::string status();
}
