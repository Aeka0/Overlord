#pragma once
#include "../scripted_sequences.hpp"
#include <string_view>

namespace vr::gameplay::sequences::airport
{
    struct evidence
    {
        bool boarding{},shot{};
        unsigned parent{},body{};
        std::string_view rig;
    };
    inline view classify(const evidence& e) noexcept
    {
        if((!e.boarding && !e.shot) || !e.parent || e.parent!=e.body || e.rig!="player_ending")return {};
        auto result=free_look(scenario::airport,e.shot?phase::execution:phase::hookup);
        result.camera=e.shot?scene_cameras::airport_shot:scene_cameras::airport_boarding;
        result.rotation_tag=game_view::scripted_camera_tag::player;
        // The native sequence has already frozen controls and removed weapons.
        // Keep its animated body/props while suppressing interactive VR input.
        result.suspend_weapons=true;
        result.allow_movement=result.allow_turn=false;
        return result;
    }
    bool supported();
    view observe();
}
