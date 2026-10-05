#include <std_include.hpp>
#include "ending.hpp"
#include "../ending_runtime.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::ending
{
    bool supported()
    {
        const auto* map=game::Dvar_FindVar("mapname");
        return map && map->current.string && std::string_view(map->current.string)=="ending";
    }
    view observe()
    {
        namespace story=::vr::gameplay::ending;
        const scripting::entity level{*game::levelEntityId},player{game::scr_entref_t{0,0}};
        const auto mode=scripting::get_object_variable(*game::levelEntityId,0xAC38u),camera=level.get("camera"),flags=level.get("flag");
        const auto* rolling=game::Dvar_FindVar("credits_active");
        if(mode.is<std::string>() && camera.is<scripting::entity>() && rolling && rolling->current.enabled && player.call("islinked").as<int>())
        {
            const auto parent=player.call("getlinkedparent");bool transitioned{};
            if(flags.is<scripting::array>()){const auto value=flags.as<scripting::array>().get(std::string("do_museum_credits"));transitioned=value.is<int>() && value.as<int>();}
            const bool same=parent.is<scripting::entity>() && parent.as<scripting::entity>().get_entity_id()==camera.as<scripting::entity>().get_entity_id();
            if(story::museum_camera(mode.as<std::string>(),transitioned,same))
            {
                auto result=free_look(scenario::museum_credits);result.camera=scene_cameras::museum_credits;
                result.suspend_weapons=true;result.allow_movement=result.allow_turn=false;return result;
            }
        }
        const auto state=story::latest();if(!state.active)return {};
        auto result=presentation(state.phase);
        result.independent_hands=state.independent;
        if(state.hide_body)result.hidden_entities={state.body,state.secondary_body,state.knife,state.waiting_price};
        else if(state.throwing_blade)result.hidden_entities[2]=state.knife;
        result.hidden_entities[3]=state.waiting_price;
        if(state.phase==story::stage::crawl && state.carrier>0)result.rotation_tag=game_view::scripted_camera_tag::origin;
        if(state.helper_camera)result.rotation_tag=game_view::scripted_camera_tag::origin;
        if(story::body_control(state.phase) && state.body>0)
        {
            result.arms={scripted_arms::model_profile::ending_injured,scripted_arms::control::tracked,state.body,3};
            result.arms.reference=state.reference;
            result.arms.outward_elbows=story::outward_elbow_hands(state.phase);
            result.arms.locked=state.held;result.arms.grasp_hands=state.throwing_blade?2u:state.held;
            result.arms.grips=state.grips;result.arms.hand_pose=state.hand_pose;
            if(state.throwing_blade)
            {
                result.arms.attached_model="weapon_commando_knife_bloody";result.arms.attached_bone="tag_knife";
                result.arms.attached_hand=1;result.arms.attached_in_wrist=state.knife_attachment;
            }
        }
        return result;
    }
}
