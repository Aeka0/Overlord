#pragma once
#include "cliffhanger_profile.hpp"
#include "component/scene_rigid_part.hpp"
#include "game/assets.hpp"
namespace vr::gameplay::equipment::special::cliffhanger
{
    struct model
    {
        std::array<hands::vec,2> pick_tips{};
        game::XModel* source{};
        game::XModel skin{};
        game::XSurface surface{};
        game::Material* material{};
        // Immutable deformations at 120 Hz, including original rubber weights.
        // The native 30 Hz keys are interpolated before baking; no queued GPU
        // buffer or source asset is ever edited during playback.
        static constexpr unsigned steps=101;
        scene_models::rigid_part idle;
        std::array<scene_models::rigid_part,steps> fire;
        std::array<scene_models::rigid_part,2> picks;
        hands::vec center{};
        bool create(game::XModel*,const std::array<game::XModel*,2>&,bool include_picks=true);
        game::XModel* detonator(float elapsed) noexcept
        {return elapsed<0?idle.model():fire[std::min(unsigned(std::lround(std::clamp(elapsed,0.f,25.f/30.f)*120.f)),steps-1)].model();}
    };
}
